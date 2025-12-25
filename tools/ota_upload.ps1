param(
  [Parameter(Mandatory = $false)]
  [string]$Ip = "",

  [Parameter(Mandatory = $false)]
  [string]$Bin = "build/esp32-idf5-pump5-v3.bin",

  [Parameter(Mandatory = $false)]
  [int]$TimeoutSec = 180
)

$ErrorActionPreference = "Stop"

function Get-OtaCacheFile {
  return (Join-Path $PSScriptRoot ".ota_last_ip")
}

function Load-LastIp {
  $f = Get-OtaCacheFile
  if (Test-Path -LiteralPath $f) {
    try {
      $ip = (Get-Content -LiteralPath $f -Raw).Trim()
      if ($ip -match '^\d{1,3}(\.\d{1,3}){3}$') { return $ip }
    } catch {}
  }
  return $null
}

function Save-LastIp([string]$ip) {
  try { Set-Content -LiteralPath (Get-OtaCacheFile) -Value $ip -NoNewline -Encoding ascii } catch {}
}

function Test-Device([string]$hostOrIp) {
  # Проверяем, что это "наше" устройство: /info.json отвечает.
  $url = "http://$hostOrIp/info.json"
  $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
  if ($curl) {
    # -m: общий таймаут, --connect-timeout: таймаут коннекта
    $out = & $curl.Source "-s" "-m" "2" "--connect-timeout" "1" "$url"
    if (-not $out) { return $false }
    # Достаточно того, что это валидный json с полями ip/mac/app (простая эвристика)
    if ($out -match '"ip"\s*:\s*"' -and $out -match '"mac"\s*:\s*"' ) { return $true }
    return $false
  }
  try {
    $j = Invoke-RestMethod -Uri $url -Method Get -TimeoutSec 2
    return ($null -ne $j.ip -and $null -ne $j.mac)
  } catch {
    return $false
  }
}

function Get-LocalIPv4Prefix {
  # Берём интерфейс дефолтного маршрута
  try {
    $route = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction Stop | Sort-Object RouteMetric | Select-Object -First 1
    if (-not $route) { return $null }
    $ip = Get-NetIPAddress -InterfaceIndex $route.InterfaceIndex -AddressFamily IPv4 -ErrorAction Stop |
      Where-Object { $_.IPAddress -and $_.PrefixLength -ge 16 } |
      Sort-Object PrefixLength -Descending |
      Select-Object -First 1
    if (-not $ip) { return $null }
    return @{ IP = $ip.IPAddress; Prefix = [int]$ip.PrefixLength }
  } catch {
    return $null
  }
}

function Get-Network24([string]$ip) {
  $p = $ip.Split('.')
  if ($p.Count -ne 4) { return $null }
  return "$($p[0]).$($p[1]).$($p[2]).0/24"
}

function Find-DeviceHost {
  if ($Ip -and $Ip.Trim().Length -gt 0) {
    if (Test-Device $Ip) { return $Ip }
    throw "Device at $Ip not reachable (/info.json)."
  }
  throw "Missing -Ip. Set it in .vscode/settings.json (pump.ota.ip) or pass -Ip explicitly."
}

function Resolve-AppBin([string]$binArg) {
  # Если пользователь передал строку вида build/${command:...}.bin или путь не существует —
  # берём самый свежий app *.bin из build/, исключая bootloader/partition/storage.
  if ($binArg -match '\$\{command:' -or -not (Test-Path -LiteralPath $binArg)) {
    $cand = Get-ChildItem -Path "build" -Filter "*.bin" -File |
      Where-Object { $_.Name -notmatch "bootloader|partition|storage" } |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
    if (-not $cand) { throw "No app .bin found in build/" }
    return $cand.FullName
  }
  return (Resolve-Path -LiteralPath $binArg).Path
}

function Get-Info([string]$hostOrIp) {
  $url = "http://$hostOrIp/info.json"
  try {
    return Invoke-RestMethod -Uri $url -Method Get -TimeoutSec 3
  } catch {
    return $null
  }
}

function Get-Fingerprint($info) {
  if (-not $info) { return "" }
  $v = "" + ($info.version  | ForEach-Object { $_ })
  $d = "" + ($info.build_date | ForEach-Object { $_ })
  $t = "" + ($info.build_time | ForEach-Object { $_ })
  $s = "" + ($info.elf_sha8  | ForEach-Object { $_ })
  return "$v|$d $t|$s"
}

function Get-HomeHtml([string]$hostOrIp) {
  $url = "http://$hostOrIp/"
  try {
    return Invoke-WebRequest -Uri $url -Method Get -TimeoutSec 4 | Select-Object -ExpandProperty Content
  } catch {
    return $null
  }
}

function Home-Has-Ticks($html) {
  if (-not $html) { return $false }
  return ($html -match 'id="ticks"' -and $html -match 'id="target"')
}

function Resolve-StorageBin([string]$appBinPath) {
  # Обычно storage.bin лежит в build/storage.bin. Если нет — пропускаем.
  $cand = Join-Path (Split-Path -Parent $appBinPath) "storage.bin"
  if (Test-Path -LiteralPath $cand) { return $cand }
  return $null
}

function Post-Binary([string]$url, [string]$filePath, [int]$timeoutSec) {
  $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
  if (-not $curl) { throw "curl.exe not found" }
  # -f: считать HTTP>=400 ошибкой (curl exit=22)
  & $curl.Source "-f" "-X" "POST" "-H" "Content-Type: application/octet-stream" "--data-binary" "@$filePath" "--max-time" "$timeoutSec" "$url" | Out-Null
  return [int]$LASTEXITCODE
}

$binPath = Resolve-AppBin $Bin
$deviceHost = Find-DeviceHost
if ($deviceHost -match '^\d{1,3}(\.\d{1,3}){3}$') { Save-LastIp $deviceHost }
$uriApp = "http://$deviceHost/api/ota"
$uriStorage = "http://$deviceHost/api/ota/storage"

Write-Host ("Device: {0}" -f $deviceHost)
$before = Get-Info $deviceHost
$beforeFp = Get-Fingerprint $before
$beforeUpt = 0
try { $beforeUpt = [int]$before.uptime_s } catch { $beforeUpt = 0 }
if ($before) {
  Write-Host ("Before: version={0} build={1} {2} sha={3} uptime={4}s" -f $before.version, $before.build_date, $before.build_time, $before.elf_sha8, $before.uptime_s)
} else {
  Write-Host "Before: cannot read /info.json (will still try OTA)"
}

$storageBin = Resolve-StorageBin $binPath
Write-Host ("Uploading app {0} -> {1}" -f $binPath, $uriApp)

# Важно: InFile шлёт бинарник как есть (без multipart), как ожидает /api/ota.
# Но Invoke-WebRequest/HttpClient иногда подвисает на больших payload'ах (Expect: 100-continue).
# Поэтому предпочитаем curl.exe (есть в Win10+).
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($curl) {
  # Важно: ESP может перезагрузиться и сбросить соединение сразу после OK.
  # Поэтому коды 52/56 (reset) и 28 (timeout) не считаем фатальными — дальше проверим по /info.json.
  & $curl.Source "-X" "POST" "-H" "Content-Type: application/octet-stream" "--data-binary" "@$binPath" "--max-time" "$TimeoutSec" "$uriApp"
  $code = $LASTEXITCODE
  if ($code -ne 0 -and $code -ne 52 -and $code -ne 56 -and $code -ne 28) {
    exit $code
  }
  Write-Host ("Upload finished (curl exit={0}). Waiting for reboot..." -f $code)

  $deadline = (Get-Date).AddSeconds([Math]::Max(10, $TimeoutSec))
  Start-Sleep -Milliseconds 800
  $seenDown = $false
  $appOk = $false
  $afterInfo = $null
  while ((Get-Date) -lt $deadline) {
    $now = Get-Info $deviceHost
    if (-not $now) {
      $seenDown = $true
      Start-Sleep -Milliseconds 700
      continue
    }
    $nowFp = Get-Fingerprint $now
    $upt = 0
    try { $upt = [int]$now.uptime_s } catch { $upt = 0 }

    # Успех: изменился fingerprint (version/build/sha), либо устройство действительно перезагрузилось (uptime маленький)
    if (($beforeFp -and $nowFp -and ($nowFp -ne $beforeFp)) -or
        ($seenDown -and $upt -ge 0 -and $upt -lt 120) -or
        (($beforeUpt -ge 30) -and ($upt -ge 0) -and ($upt -lt 30))) {
      Write-Host ("After: version={0} build={1} {2} sha={3} uptime={4}s" -f $now.version, $now.build_date, $now.build_time, $now.elf_sha8, $now.uptime_s)
      Write-Host "OTA OK"
      $appOk = $true
      $afterInfo = $now
      break
    }

    # Ещё не успели перезагрузиться/обновиться
    Start-Sleep -Milliseconds 900
  }

  if (-not $appOk) {
    Write-Host "OTA uncertain: device did not reboot/identify new build within timeout."
    exit 2
  }
}

# --- Upload storage (SPIFFS) after app (optional) ---
if ($storageBin) {
  Write-Host ("Uploading storage {0} -> {1}" -f $storageBin, $uriStorage)
  $code = Post-Binary $uriStorage $storageBin $TimeoutSec
  # На прошивках без endpoint получим curl exit=22 (HTTP 404).
  if ($code -eq 22) {
    Write-Host "Storage OTA endpoint not available yet (HTTP error)."
    exit 0
  }
  if ($code -ne 0 -and $code -ne 52 -and $code -ne 56 -and $code -ne 28) {
    Write-Host ("Storage upload failed (curl exit={0})" -f $code)
    exit $code
  }
  Write-Host ("Storage upload finished (curl exit={0}). Waiting for reboot..." -f $code)
  Start-Sleep -Seconds 2

  # Проверим, что главная страница теперь содержит поля ticks/target
  $html = Get-HomeHtml $deviceHost
  if (Home-Has-Ticks $html) {
    Write-Host "UI OK (ticks visible on /)"
  } else {
    Write-Host "UI uncertain: / does not contain ticks fields yet (try Ctrl+F5 in browser)."
  }
} else {
  Write-Host "No storage.bin found (skip storage upload)."
}

# Если curl отсутствует — используем старый путь (только app OTA)
if (-not $curl) {
  $resp = Invoke-WebRequest -Uri $uriApp -Method Post -ContentType "application/octet-stream" -InFile $binPath -TimeoutSec $TimeoutSec
  Write-Host $resp.Content
}

exit 0

$resp = Invoke-WebRequest -Uri $uri -Method Post -ContentType "application/octet-stream" -InFile $binPath -TimeoutSec $TimeoutSec
Write-Host $resp.Content


