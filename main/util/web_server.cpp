#include "web_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "i2c.h"
#include "main.h"
#include "task/counterTask.h"

#include "pcf8575_io.h"
#include "spiffs_fs.h"
#include "web_log.h"

static const char *TAG = "WEB_SERVER";

static httpd_handle_t s_server = nullptr;
static bool s_ota_in_progress = false;

static void format_mac(const uint8_t mac[6], char *out, size_t out_len) {
  if (!out || out_len < 18) return;
  snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void get_sta_ip_str(char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = '\0';

  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) return;

  esp_netif_ip_info_t ip = {};
  if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return;

  snprintf(out, out_len, IPSTR, IP2STR(&ip.ip));
}

static int get_rssi_dbm() {
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    return ap.rssi;
  }
  return 0;
}

static const char *content_type_for(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext) return "application/octet-stream";
  if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
  if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
  if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
  if (strcmp(ext, ".json") == 0) return "application/json";
  if (strcmp(ext, ".png") == 0) return "image/png";
  if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
  return "application/octet-stream";
}

static esp_err_t send_spiffs_error_page(httpd_req_t *req, const char *reason) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_status(req, "200 OK");

  // Небольшая страница-подсказка, когда SPIFFS (storage) не смонтирован / пустой.
  // Важно: не собираем страницу через snprintf в фиксированный буфер — в IDF сборках часто включён
  // -Werror=format-truncation, и это ломает билд. Шлём HTML чанками.
  const char *r = (reason && reason[0]) ? reason : "unknown";

  esp_err_t err = ESP_OK;
  err = httpd_resp_sendstr_chunk(req,
                                 "<!doctype html><html><head><meta charset='utf-8'>"
                                 "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                                 "<title>PUMP Web</title>"
                                 "<style>"
                                 "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:24px;max-width:820px}"
                                 "code{background:#f2f2f2;padding:2px 6px;border-radius:6px}"
                                 ".box{border:1px solid #ddd;border-radius:12px;padding:16px}"
                                 "h1{margin:0 0 8px 0;font-size:20px}"
                                 "p{margin:8px 0;line-height:1.4}"
                                 "</style></head><body><div class='box'>"
                                 "<h1>Web UI недоступен (SPIFFS)</h1>"
                                 "<p>Раздел <code>storage</code> (SPIFFS) не смонтировался или не содержит файлов веб‑морды.</p>"
                                 "<p><b>Причина:</b> <code>");
  if (err != ESP_OK) return err;
  err = httpd_resp_send_chunk(req, r, HTTPD_RESP_USE_STRLEN);
  if (err != ESP_OK) return err;
  err = httpd_resp_sendstr_chunk(req,
                                 "</code></p>"
                                 "<p><b>Что сделать:</b> залить <code>storage.bin</code> через <code>/api/ota/storage</code> "
                                 "или выполнить <code>tools/ota_upload.ps1</code>.</p>"
                                 "<p>API без UI: <code>/info.json</code>, <code>/api/config</code>, <code>/api/stats</code>, <code>/api/log</code>.</p>"
                                 "</div></body></html>");
  if (err != ESP_OK) return err;
  return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t send_file_from_spiffs(httpd_req_t *req, const char *rel_uri) {
  const esp_err_t m = spiffs_fs_mount();
  if (m != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(m));
    return send_spiffs_error_page(req, esp_err_to_name(m));
  }

  const char *uri = rel_uri ? rel_uri : req->uri;
  char path[256];

  if (!uri || uri[0] == '\0' || strcmp(uri, "/") == 0) {
    const int n = snprintf(path, sizeof(path), "%s/index.html", spiffs_fs_base_path());
    if (n <= 0 || n >= (int)sizeof(path)) {
      httpd_resp_set_status(req, "414 URI Too Long");
      return httpd_resp_send(req, "path too long", HTTPD_RESP_USE_STRLEN);
    }
  } else {
    // Защита от .. в uri
    if (strstr(uri, "..")) {
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "bad uri", HTTPD_RESP_USE_STRLEN);
    }
    // Избегаем format-truncation (-Werror) и явного переполнения буфера
    const size_t base_len = strlen(spiffs_fs_base_path());
    const size_t uri_len = strlen(uri);
    if (base_len + uri_len >= sizeof(path)) {
      httpd_resp_set_status(req, "414 URI Too Long");
      return httpd_resp_send(req, "uri too long", HTTPD_RESP_USE_STRLEN);
    }
    memcpy(path, spiffs_fs_base_path(), base_len);
    memcpy(path + base_len, uri, uri_len + 1); // включая '\0'
  }

  struct stat st;
  if (stat(path, &st) != 0 || st.st_size <= 0) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_send(req, "not found", HTTPD_RESP_USE_STRLEN);
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "open failed", HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_set_type(req, content_type_for(path));

  char buf[1024];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    const esp_err_t err = httpd_resp_send_chunk(req, buf, n);
    if (err != ESP_OK) {
      fclose(f);
      httpd_resp_sendstr_chunk(req, nullptr);
      return err;
    }
  }
  fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t info_json_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  char ip[32];
  get_sta_ip_str(ip, sizeof(ip));

  uint8_t mac[6] = {0};
  char mac_s[32] = {0};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    format_mac(mac, mac_s, sizeof(mac_s));
  }

  const uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  const uint32_t heap = esp_get_free_heap_size();
  const int rssi = get_rssi_dbm();

  const esp_app_desc_t *app = esp_app_get_description();

  // app->date/app->time/app_elf_sha256 помогают однозначно отличать сборки (особенно когда version не меняется).
  // app_elf_sha256: 32 байта, выведем первые 8 байт (16 hex-символов) для компактности.
  char sha8[17] = {0};
  if (app) {
    snprintf(sha8, sizeof(sha8),
             "%02X%02X%02X%02X%02X%02X%02X%02X",
             (unsigned)app->app_elf_sha256[0], (unsigned)app->app_elf_sha256[1],
             (unsigned)app->app_elf_sha256[2], (unsigned)app->app_elf_sha256[3],
             (unsigned)app->app_elf_sha256[4], (unsigned)app->app_elf_sha256[5],
             (unsigned)app->app_elf_sha256[6], (unsigned)app->app_elf_sha256[7]);
  }

  char body[512];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ip\":\"%s\","
                         "\"mac\":\"%s\","
                         "\"rssi_dbm\":%d,"
                         "\"free_heap\":%" PRIu32 ","
                         "\"uptime_s\":%" PRIu32 ","
                         "\"app\":\"%s\","
                         "\"version\":\"%s\","
                         "\"idf\":\"%s\","
                         "\"build_date\":\"%s\","
                         "\"build_time\":\"%s\","
                         "\"elf_sha8\":\"%s\""
                         "}",
                         ip[0] ? ip : "",
                         mac_s[0] ? mac_s : "",
                         rssi,
                         heap,
                         uptime_s,
                         app ? app->project_name : "",
                         app ? app->version : "",
                         app ? app->idf_ver : "",
                         app ? app->date : "",
                         app ? app->time : "",
                         sha8);
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

// Для удобства: /status -> то же самое, что /info.json
static esp_err_t status_json_get_handler(httpd_req_t *req) {
  return info_json_get_handler(req);
}

static esp_err_t api_status_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  // Веб должен показывать/управлять расширителем, а не GPIO ESP32
  const bool pump = ioexp_get_pump();
  const bool p1 = ioexp_get_valve(1);
  const bool p2 = ioexp_get_valve(2);
  const bool p3 = ioexp_get_valve(3);
  const bool p4 = ioexp_get_valve(4);

  char body[320];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"pump\":%d,"
                         "\"p1\":%d,"
                         "\"p2\":%d,"
                         "\"p3\":%d,"
                         "\"p4\":%d"
                         "}",
                         pump ? 1 : 0, p1 ? 1 : 0, p2 ? 1 : 0, p3 ? 1 : 0, p4 ? 1 : 0);
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_stats_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  char body[256];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ok\":1,"
                         "\"ticks\":%ld,"
                         "\"water_target\":%ld,"
                         "\"banks_run\":%ld,"
                         "\"banks_today\":%ld,"
                         "\"banks_total\":%ld"
                         "}",
                         (long)app_state.water_current,
                         (long)app_state.water_target,
                         (long)app_state.banks_count,
                         (long)app_state.today_banks_count,
                         (long)app_state.total_banks_count);
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_stats_post_handler(httpd_req_t *req) {
  // Чтобы не ломать текущий цикл — запрещаем менять счётчики во время работы
  if (app_state.is_on) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "running", HTTPD_RESP_USE_STRLEN);
  }

  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing query", HTTPD_RESP_USE_STRLEN);
  }

  auto get_i32 = [&](const char *key, int32_t *out) -> bool {
    char tmp[24];
    if (httpd_query_key_value(query, key, tmp, sizeof(tmp)) != ESP_OK) return false;
    *out = (int32_t)strtol(tmp, nullptr, 10);
    return true;
  };

  int32_t today = app_state.today_banks_count;
  int32_t total = app_state.total_banks_count;

  int32_t v = 0;
  if (get_i32("today", &v)) today = v;
  if (get_i32("total", &v)) total = v;

  if (today < 0 || today > 100000000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad today", HTTPD_RESP_USE_STRLEN);
  }
  if (total < 0 || total > 100000000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad total", HTTPD_RESP_USE_STRLEN);
  }
  if (today > total) {
    // не запрещаем жёстко, но это почти всегда ошибка
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "today > total", HTTPD_RESP_USE_STRLEN);
  }

  // Пишем в NVS
  esp_err_t err = save_today_banks_count(today);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save today failed", HTTPD_RESP_USE_STRLEN);
  }
  err = save_total_banks_count(total);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save total failed", HTTPD_RESP_USE_STRLEN);
  }

  // Обновляем runtime состояние
  app_state.today_banks_count = today;
  app_state.total_banks_count = total;

  return api_stats_get_handler(req);
}

static esp_err_t api_ticks_reset_post_handler(httpd_req_t *req) {
  // Сбрасывать тики безопасно только в idle, чтобы не ломать текущий цикл.
  if (app_state.is_on) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "running", HTTPD_RESP_USE_STRLEN);
  }
  if (!counter) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "counter task not ready", HTTPD_RESP_USE_STRLEN);
  }

  // Запрашиваем сброс в контексте задачи counter (там же чистится PCNT/ISR state).
  xTaskNotify(counter, RESET_TICKS_BIT, eSetBits);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":1}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_ticks_level_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  const int level = gpio_get_level(COUNTER_TICK_GPIO) ? 1 : 0;
  char body[96];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ok\":1,"
                         "\"gpio\":%ld,"
                         "\"level\":%d"
                         "}",
                         (long)COUNTER_TICK_GPIO, level);
  if (n <= 0 || n >= (int)sizeof(body)) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_i2c_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  const char *sum = i2c_last_scan_summary();
  char body[192];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ok\":1,"
                         "\"summary\":\"%s\""
                         "}",
                         sum ? sum : "");
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_config_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  int32_t steps = 0, enc = 0, f1 = 0, f2 = 0;
  config_get_cached_pump_settings(&steps, &enc, &f1, &f2);
  int32_t voff[NUM_VALVES] = {0};
  config_get_cached_valve_offsets(voff);
  int32_t dry_ms = 0, dry_min = 0;
  config_get_cached_dry_run(&dry_ms, &dry_min);
  int32_t tick_source = 0, tick_min_us = 0, tick_pull = 0;
  config_get_cached_tick_counter(&tick_source, &tick_min_us, &tick_pull);
  const int32_t target = steps + enc;

  char body[768];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ok\":1,"
                         "\"steps\":%ld,"
                         "\"encoder\":%ld,"
                         "\"water_target\":%ld,"
                         "\"valve_off\":[%ld,%ld,%ld,%ld],"
                         "\"valve_target\":[%ld,%ld,%ld,%ld],"
                         "\"flush_valve_ms\":%ld,"
                         "\"flush_all_ms\":%ld,"
                         "\"dry_run_timeout_ms\":%ld,"
                         "\"dry_run_min_ticks\":%ld,"
                         "\"tick_source\":%ld,"
                         "\"tick_min_interval_us\":%ld,"
                         "\"tick_pull\":%ld,"
                         "\"tick_gpio\":%ld"
                         "}",
                         (long)steps, (long)enc, (long)target,
                         (long)voff[0], (long)voff[1], (long)voff[2], (long)voff[3],
                         (long)(target + voff[0]), (long)(target + voff[1]), (long)(target + voff[2]), (long)(target + voff[3]),
                         (long)f1, (long)f2,
                         (long)dry_ms, (long)dry_min,
                         (long)tick_source, (long)tick_min_us, (long)tick_pull,
                         (long)COUNTER_TICK_GPIO);
  if (n <= 0 || n >= (int)sizeof(body)) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_config_post_handler(httpd_req_t *req) {
  // Уставки меняем только в состоянии idle (чтобы не ломать текущий цикл налива/промывки)
  if (app_state.is_on) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "running", HTTPD_RESP_USE_STRLEN);
  }

  char query[384];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing query", HTTPD_RESP_USE_STRLEN);
  }

  auto get_i32 = [&](const char *key, int32_t *out) -> bool {
    char tmp[24];
    if (httpd_query_key_value(query, key, tmp, sizeof(tmp)) != ESP_OK) return false;
    *out = (int32_t)strtol(tmp, nullptr, 10);
    return true;
  };

  int32_t steps = 0, enc = 0, f1 = 0, f2 = 0;
  config_get_cached_pump_settings(&steps, &enc, &f1, &f2);
  int32_t voff[NUM_VALVES] = {0};
  config_get_cached_valve_offsets(voff);
  int32_t dry_ms = 0, dry_min = 0;
  config_get_cached_dry_run(&dry_ms, &dry_min);
  int32_t tick_source = 0, tick_min_us = 0, tick_pull = 0;
  config_get_cached_tick_counter(&tick_source, &tick_min_us, &tick_pull);

  int32_t v = 0;
  if (get_i32("steps", &v)) steps = v;
  if (get_i32("encoder", &v)) enc = v;
  if (get_i32("valve_off1", &v)) voff[0] = v;
  if (get_i32("valve_off2", &v)) voff[1] = v;
  if (get_i32("valve_off3", &v)) voff[2] = v;
  if (get_i32("valve_off4", &v)) voff[3] = v;
  if (get_i32("flush_valve_ms", &v)) f1 = v;
  if (get_i32("flush_all_ms", &v)) f2 = v;
  if (get_i32("dry_run_timeout_ms", &v)) dry_ms = v;
  if (get_i32("dry_run_min_ticks", &v)) dry_min = v;
  if (get_i32("tick_source", &v)) tick_source = v;
  if (get_i32("tick_min_interval_us", &v)) tick_min_us = v;
  // Новый ключ
  if (get_i32("tick_pull", &v)) tick_pull = v;
  // Совместимость со старым ключом
  if (get_i32("tick_pullup", &v)) tick_pull = (v != 0) ? 1 : 0;

  // Валидация
  if (steps < 1 || steps > 500000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad steps", HTTPD_RESP_USE_STRLEN);
  }
  if (enc < -500000 || enc > 500000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad encoder", HTTPD_RESP_USE_STRLEN);
  }
  for (int i = 0; i < NUM_VALVES; i++) {
    if (voff[i] < -500000 || voff[i] > 500000) {
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "bad valve_off", HTTPD_RESP_USE_STRLEN);
    }
  }
  if (f1 < 50 || f1 > 600000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad flush_valve_ms", HTTPD_RESP_USE_STRLEN);
  }
  if (f2 < 50 || f2 > 600000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad flush_all_ms", HTTPD_RESP_USE_STRLEN);
  }
  if (dry_ms < 200 || dry_ms > 600000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad dry_run_timeout_ms", HTTPD_RESP_USE_STRLEN);
  }
  if (dry_min < 0 || dry_min > 100000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad dry_run_min_ticks", HTTPD_RESP_USE_STRLEN);
  }
  if (tick_source < 0 || tick_source > 1) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad tick_source", HTTPD_RESP_USE_STRLEN);
  }
  if (tick_min_us < 0 || tick_min_us > 500000) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad tick_min_interval_us", HTTPD_RESP_USE_STRLEN);
  }
  if (tick_pull < 0 || tick_pull > 2) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad tick_pull", HTTPD_RESP_USE_STRLEN);
  }

  const esp_err_t err = config_save_pump_settings(steps, enc, f1, f2);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save failed", HTTPD_RESP_USE_STRLEN);
  }

  const esp_err_t err2 = config_save_valve_offsets(voff);
  if (err2 != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save valve offsets failed", HTTPD_RESP_USE_STRLEN);
  }

  const esp_err_t err3 = config_save_dry_run(dry_ms, dry_min);
  if (err3 != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save dry_run failed", HTTPD_RESP_USE_STRLEN);
  }

  const esp_err_t err4 = config_save_tick_counter(tick_source, tick_min_us, tick_pull);
  if (err4 != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "save tick config failed", HTTPD_RESP_USE_STRLEN);
  }

  // Применяем в рантайме для экрана/логики
  counter_reload_runtime_settings();

  return api_config_get_handler(req);
}

static esp_err_t api_ioexp_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  if (!ioexp_is_initialized()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "{\"ok\":0,\"err\":\"ioexp not initialized\"}", HTTPD_RESP_USE_STRLEN);
  }

  uint16_t shadow = 0;
  uint16_t port = 0;
  esp_err_t err = ioexp_get_shadow(&shadow);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "{\"ok\":0,\"err\":\"get_shadow failed\"}", HTTPD_RESP_USE_STRLEN);
  }
  err = ioexp_port_read(&port);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "{\"ok\":0,\"err\":\"port_read failed\"}", HTTPD_RESP_USE_STRLEN);
  }

  char body[128];
  const int n = snprintf(body, sizeof(body),
                         "{"
                         "\"ok\":1,"
                         "\"shadow\":\"0x%04x\","
                         "\"port\":\"0x%04x\","
                         "\"shadow_u\":%u,"
                         "\"port_u\":%u"
                         "}",
                         (unsigned)shadow, (unsigned)port, (unsigned)shadow, (unsigned)port);
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_ioexp_set_post_handler(httpd_req_t *req) {
  if (!ioexp_is_initialized()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "ioexp not initialized", HTTPD_RESP_USE_STRLEN);
  }

  char query[160];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing query", HTTPD_RESP_USE_STRLEN);
  }

  char bit_s[8];
  if (httpd_query_key_value(query, "bit", bit_s, sizeof(bit_s)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing bit", HTTPD_RESP_USE_STRLEN);
  }
  const int bit = (int)strtol(bit_s, nullptr, 10);
  if (bit < 0 || bit > 15) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bit out of range (0..15)", HTTPD_RESP_USE_STRLEN);
  }

  esp_err_t err = ESP_OK;

  char toggle_s[8];
  if (httpd_query_key_value(query, "toggle", toggle_s, sizeof(toggle_s)) == ESP_OK) {
    const int t = (int)strtol(toggle_s, nullptr, 10);
    if (t != 0) err = ioexp_toggle_bit_raw(bit);
  } else {
    char val_s[8];
    if (httpd_query_key_value(query, "val", val_s, sizeof(val_s)) != ESP_OK) {
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "missing val or toggle", HTTPD_RESP_USE_STRLEN);
    }
    const int v = (int)strtol(val_s, nullptr, 10);
    if (v != 0 && v != 1) {
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "val must be 0 or 1", HTTPD_RESP_USE_STRLEN);
    }
    err = ioexp_set_bit_raw(bit, v ? true : false);
  }

  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "ioexp set failed", HTTPD_RESP_USE_STRLEN);
  }

  return api_ioexp_get_handler(req);
}

static esp_err_t api_toggle_post_handler(httpd_req_t *req) {
  if (!ioexp_is_initialized()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "ioexp not initialized", HTTPD_RESP_USE_STRLEN);
  }

  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing query", HTTPD_RESP_USE_STRLEN);
  }

  char id[16];
  if (httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "missing id", HTTPD_RESP_USE_STRLEN);
  }

  esp_err_t err = ESP_OK;
  if (strcmp(id, "pump") == 0) {
    err = ioexp_toggle_pump();
  } else if (strcmp(id, "p1") == 0) {
    err = ioexp_toggle_valve(1);
  } else if (strcmp(id, "p2") == 0) {
    err = ioexp_toggle_valve(2);
  } else if (strcmp(id, "p3") == 0) {
    err = ioexp_toggle_valve(3);
  } else if (strcmp(id, "p4") == 0) {
    err = ioexp_toggle_valve(4);
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "unknown id", HTTPD_RESP_USE_STRLEN);
  }

  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "toggle failed", HTTPD_RESP_USE_STRLEN);
  }

  // Ответим текущим статусом, чтобы UI мог обновиться без отдельного запроса.
  return api_status_get_handler(req);
}

static esp_err_t api_log_get_handler(httpd_req_t *req) {
  web_log_init();
  httpd_resp_set_type(req, "text/plain; charset=utf-8");

  char query[64];
  uint32_t since = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char since_s[16];
    if (httpd_query_key_value(query, "since", since_s, sizeof(since_s)) == ESP_OK) {
      since = (uint32_t)strtoul(since_s, nullptr, 10);
    }
  }

  const uint32_t last = web_log_last_seq();
  const uint32_t earliest = web_log_earliest_seq();

  bool lost = false;
  uint32_t start = since + 1;
  if (start < earliest) {
    start = earliest;
    if (since != 0) lost = true;
  }

  char header[64];
  snprintf(header, sizeof(header), "next=%" PRIu32 "%s\n", last, lost ? " lost=1" : "");
  httpd_resp_send_chunk(req, header, HTTPD_RESP_USE_STRLEN);

  char line[196];
  for (uint32_t seq = start; seq <= last; seq++) {
    if (!web_log_get_line(seq, line, sizeof(line))) continue;
    httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "\n", 1);
  }

  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t api_ota_post_handler(httpd_req_t *req) {
  if (s_ota_in_progress) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "OTA already in progress", HTTPD_RESP_USE_STRLEN);
  }
  s_ota_in_progress = true;

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "No OTA partition", HTTPD_RESP_USE_STRLEN);
  }

  ESP_LOGW(TAG, "OTA start: writing to partition %s at offset 0x%" PRIx32,
           update_partition->label, update_partition->address);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "esp_ota_begin failed", HTTPD_RESP_USE_STRLEN);
  }

  char buf[1024];
  int remaining = req->content_len;
  int total = 0;

  while (remaining > 0) {
    const int to_read = (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining;
    const int recv = httpd_req_recv(req, buf, to_read);
    if (recv == HTTPD_SOCK_ERR_TIMEOUT) {
      // просто ждём дальше
      continue;
    }
    if (recv <= 0) {
      esp_ota_abort(ota_handle);
      s_ota_in_progress = false;
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "recv failed", HTTPD_RESP_USE_STRLEN);
    }
    err = esp_ota_write(ota_handle, buf, recv);
    if (err != ESP_OK) {
      esp_ota_abort(ota_handle);
      s_ota_in_progress = false;
      httpd_resp_set_status(req, "500 Internal Server Error");
      return httpd_resp_send(req, "esp_ota_write failed", HTTPD_RESP_USE_STRLEN);
    }
    remaining -= recv;
    total += recv;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "esp_ota_end failed", HTTPD_RESP_USE_STRLEN);
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "esp_ota_set_boot_partition failed", HTTPD_RESP_USE_STRLEN);
  }

  ESP_LOGW(TAG, "OTA done: %d bytes -> boot=%s, rebooting...", total, update_partition->label);

  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

  // Дадим стеку отправить ответ и перезагрузимся
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

static esp_err_t api_ota_storage_post_handler(httpd_req_t *req) {
  // Принимаем storage.bin (SPIFFS image) и перезаписываем партицию "storage", затем reboot.
  // Важно: SPIFFS должен быть размонтирован перед записью.

  if (s_ota_in_progress) {
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "OTA already in progress", HTTPD_RESP_USE_STRLEN);
  }
  s_ota_in_progress = true;

  const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                                         spiffs_fs_partition_label());
  if (!part) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "storage partition not found", HTTPD_RESP_USE_STRLEN);
  }

  if (req->content_len <= 0 || (size_t)req->content_len > part->size) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad content length", HTTPD_RESP_USE_STRLEN);
  }

  // Размонтируем SPIFFS, иначе запись поверх смонтированной FS опасна
  (void)spiffs_fs_unmount();

  ESP_LOGW(TAG, "OTA storage start: writing %d bytes -> partition %s at 0x%" PRIx32 " size=%" PRIu32,
           req->content_len, part->label, part->address, (uint32_t)part->size);

  // Erase partition полностью (чтобы точно очистить хвост)
  esp_err_t err = esp_partition_erase_range(part, 0, part->size);
  if (err != ESP_OK) {
    s_ota_in_progress = false;
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "erase failed", HTTPD_RESP_USE_STRLEN);
  }

  char buf[1024];
  int remaining = req->content_len;
  size_t offset = 0;
  while (remaining > 0) {
    const int to_read = (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining;
    const int recv = httpd_req_recv(req, buf, to_read);
    if (recv == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (recv <= 0) {
      s_ota_in_progress = false;
      httpd_resp_set_status(req, "400 Bad Request");
      return httpd_resp_send(req, "recv failed", HTTPD_RESP_USE_STRLEN);
    }
    err = esp_partition_write(part, offset, buf, (size_t)recv);
    if (err != ESP_OK) {
      s_ota_in_progress = false;
      httpd_resp_set_status(req, "500 Internal Server Error");
      return httpd_resp_send(req, "write failed", HTTPD_RESP_USE_STRLEN);
    }
    offset += (size_t)recv;
    remaining -= recv;
  }

  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

  ESP_LOGW(TAG, "OTA storage done: %u bytes -> rebooting...", (unsigned)offset);
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

static esp_err_t wildcard_get_handler(httpd_req_t *req) {
  return send_file_from_spiffs(req, nullptr);
}

esp_err_t web_server_start(void) {
  if (s_server) return ESP_OK;

  web_log_init();

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  // OTA иногда идёт медленно по Wi‑Fi; увеличим таймауты приёма/отправки, чтобы не рвать соединение.
  config.recv_wait_timeout = 20;
  config.send_wait_timeout = 20;
  // У нас много эндпоинтов (/info.json, /api/*, /*). Дефолтного лимита не хватает,
  // из-за чего wildcard не регистрируется и даже "/" начинает отдавать 404.
  config.max_uri_handlers = 24;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    s_server = nullptr;
    return err;
  }

  // /info.json
  httpd_uri_t info = {};
  info.uri = "/info.json";
  info.method = HTTP_GET;
  info.handler = info_json_get_handler;
  httpd_register_uri_handler(s_server, &info);

  // /status (alias)
  httpd_uri_t status_json = {};
  status_json.uri = "/status";
  status_json.method = HTTP_GET;
  status_json.handler = status_json_get_handler;
  httpd_register_uri_handler(s_server, &status_json);

  // /api/status
  httpd_uri_t status = {};
  status.uri = "/api/status";
  status.method = HTTP_GET;
  status.handler = api_status_get_handler;
  httpd_register_uri_handler(s_server, &status);

  // /api/stats
  httpd_uri_t stats = {};
  stats.uri = "/api/stats";
  stats.method = HTTP_GET;
  stats.handler = api_stats_get_handler;
  httpd_register_uri_handler(s_server, &stats);

  httpd_uri_t stats_set = {};
  stats_set.uri = "/api/stats";
  stats_set.method = HTTP_POST;
  stats_set.handler = api_stats_post_handler;
  httpd_register_uri_handler(s_server, &stats_set);

  // /api/ticks/reset
  httpd_uri_t ticks_reset = {};
  ticks_reset.uri = "/api/ticks/reset";
  ticks_reset.method = HTTP_POST;
  ticks_reset.handler = api_ticks_reset_post_handler;
  httpd_register_uri_handler(s_server, &ticks_reset);

  // /api/ticks/level
  httpd_uri_t ticks_level = {};
  ticks_level.uri = "/api/ticks/level";
  ticks_level.method = HTTP_GET;
  ticks_level.handler = api_ticks_level_get_handler;
  httpd_register_uri_handler(s_server, &ticks_level);

  // /api/toggle
  httpd_uri_t toggle = {};
  toggle.uri = "/api/toggle";
  toggle.method = HTTP_POST;
  toggle.handler = api_toggle_post_handler;
  httpd_register_uri_handler(s_server, &toggle);

  // /api/i2c
  httpd_uri_t i2c = {};
  i2c.uri = "/api/i2c";
  i2c.method = HTTP_GET;
  i2c.handler = api_i2c_get_handler;
  httpd_register_uri_handler(s_server, &i2c);

  // /api/config
  httpd_uri_t cfg_get = {};
  cfg_get.uri = "/api/config";
  cfg_get.method = HTTP_GET;
  cfg_get.handler = api_config_get_handler;
  httpd_register_uri_handler(s_server, &cfg_get);

  httpd_uri_t cfg_set = {};
  cfg_set.uri = "/api/config";
  cfg_set.method = HTTP_POST;
  cfg_set.handler = api_config_post_handler;
  httpd_register_uri_handler(s_server, &cfg_set);

  // /api/ioexp
  httpd_uri_t ioexp = {};
  ioexp.uri = "/api/ioexp";
  ioexp.method = HTTP_GET;
  ioexp.handler = api_ioexp_get_handler;
  httpd_register_uri_handler(s_server, &ioexp);

  // /api/ioexp/set
  httpd_uri_t ioexp_set = {};
  ioexp_set.uri = "/api/ioexp/set";
  ioexp_set.method = HTTP_POST;
  ioexp_set.handler = api_ioexp_set_post_handler;
  httpd_register_uri_handler(s_server, &ioexp_set);

  // /api/log
  httpd_uri_t logu = {};
  logu.uri = "/api/log";
  logu.method = HTTP_GET;
  logu.handler = api_log_get_handler;
  httpd_register_uri_handler(s_server, &logu);

  // /api/ota
  httpd_uri_t ota = {};
  ota.uri = "/api/ota";
  ota.method = HTTP_POST;
  ota.handler = api_ota_post_handler;
  httpd_register_uri_handler(s_server, &ota);

  // /api/ota/storage (SPIFFS image)
  httpd_uri_t ota_storage = {};
  ota_storage.uri = "/api/ota/storage";
  ota_storage.method = HTTP_POST;
  ota_storage.handler = api_ota_storage_post_handler;
  httpd_register_uri_handler(s_server, &ota_storage);

  // статика из SPIFFS
  httpd_uri_t files = {};
  files.uri = "/*";
  files.method = HTTP_GET;
  files.handler = wildcard_get_handler;
  httpd_register_uri_handler(s_server, &files);

  ESP_LOGI(TAG, "Web server started on :80 (no auth)");
  return ESP_OK;
}

