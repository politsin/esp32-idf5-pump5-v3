param(
  [Parameter(Mandatory = $false)]
  [string]$Ip = "192.168.3.11",

  [Parameter(Mandatory = $false)]
  [string]$Bin = "build/esp32-idf5-pump5-v3.bin",

  [Parameter(Mandatory = $false)]
  [int]$TimeoutSec = 180
)

$ErrorActionPreference = "Stop"

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

$binPath = Resolve-AppBin $Bin
$uri = "http://$Ip/api/ota"

Write-Host ("Uploading {0} -> {1}" -f $binPath, $uri)

# Важно: InFile шлёт бинарник как есть (без multipart), как ожидает /api/ota.
# Но Invoke-WebRequest/HttpClient иногда подвисает на больших payload'ах (Expect: 100-continue).
# Поэтому предпочитаем curl.exe (есть в Win10+).
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($curl) {
  & $curl.Source "-f" "-X" "POST" "-H" "Content-Type: application/octet-stream" "--data-binary" "@$binPath" "--max-time" "$TimeoutSec" "$uri"
  exit $LASTEXITCODE
}

$resp = Invoke-WebRequest -Uri $uri -Method Post -ContentType "application/octet-stream" -InFile $binPath -TimeoutSec $TimeoutSec
Write-Host $resp.Content


