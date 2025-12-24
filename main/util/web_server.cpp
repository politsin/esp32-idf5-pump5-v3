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
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static esp_err_t send_file_from_spiffs(httpd_req_t *req, const char *rel_uri) {
  (void)spiffs_fs_mount();

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
                         "\"idf\":\"%s\""
                         "}",
                         ip[0] ? ip : "",
                         mac_s[0] ? mac_s : "",
                         rssi,
                         heap,
                         uptime_s,
                         app ? app->project_name : "",
                         app ? app->version : "",
                         app ? app->idf_ver : "");
  if (n <= 0) return httpd_resp_send_500(req);
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_status_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  // Веб должен показывать/управлять расширителем, а не GPIO ESP32
  const bool pump = ioexp_get_pump();
  const bool p1 = ioexp_get_valve(1);
  const bool p2 = ioexp_get_valve(2);
  const bool p3 = ioexp_get_valve(3);
  const bool p4 = ioexp_get_valve(4);

  char body[256];
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
    const int recv = httpd_req_recv(req, buf, (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining);
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

  // /api/status
  httpd_uri_t status = {};
  status.uri = "/api/status";
  status.method = HTTP_GET;
  status.handler = api_status_get_handler;
  httpd_register_uri_handler(s_server, &status);

  // /api/toggle
  httpd_uri_t toggle = {};
  toggle.uri = "/api/toggle";
  toggle.method = HTTP_POST;
  toggle.handler = api_toggle_post_handler;
  httpd_register_uri_handler(s_server, &toggle);

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

  // статика из SPIFFS
  httpd_uri_t files = {};
  files.uri = "/*";
  files.method = HTTP_GET;
  files.handler = wildcard_get_handler;
  httpd_register_uri_handler(s_server, &files);

  ESP_LOGI(TAG, "Web server started on :80 (no auth)");
  return ESP_OK;
}


