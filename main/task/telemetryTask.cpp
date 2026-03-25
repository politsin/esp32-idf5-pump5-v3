#include "telemetryTask.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/queue.h"

#include "config.h"
#include "util/telemetry_manager.h"

static const char *TAG = "TELEMETRY_TASK";

static QueueHandle_t telemetry_queue = nullptr;
TaskHandle_t telemetryTaskHandle = nullptr;

static bool is_wifi_connected() {
  wifi_ap_record_t ap_info = {};
  return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static bool utc_time_is_valid() {
  time_t now = 0;
  time(&now);
  struct tm timeinfo = {};
  gmtime_r(&now, &timeinfo);
  return timeinfo.tm_year >= (2024 - 1900);
}

static void fill_utc_timestamp(char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = '\0';
  if (!utc_time_is_valid()) return;

  time_t now = 0;
  time(&now);
  struct tm timeinfo = {};
  gmtime_r(&now, &timeinfo);
  strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

static esp_err_t send_fill_event_http(const telemetry_fill_event_t *event) {
  if (!event) return ESP_ERR_INVALID_ARG;

  char telemetry_url[192] = {0};
  config_get_cached_telemetry(nullptr, 0, telemetry_url, sizeof(telemetry_url));
  if (telemetry_url[0] == '\0') {
    ESP_LOGW(TAG, "Telemetry URL is empty, skipping fill event seq=%ld", (long)event->fill_seq);
    return ESP_ERR_INVALID_STATE;
  }

  char event_id[128];
  snprintf(event_id, sizeof(event_id), "%s-fill-%ld",
           telemetry_resolved_device_id(), (long)event->fill_seq);

  char timestamp[32] = {0};
  fill_utc_timestamp(timestamp, sizeof(timestamp));

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "measurement", "fill_event");
  cJSON_AddStringToObject(json, "device_id", telemetry_resolved_device_id());
  if (timestamp[0] != '\0') {
    cJSON_AddStringToObject(json, "timestamp", timestamp);
  }

  cJSON *tags = cJSON_AddObjectToObject(json, "tags");
  cJSON_AddStringToObject(tags, "device_type", "filler");
  cJSON_AddStringToObject(tags, "event_type", "bank_filled");

  cJSON *fields = cJSON_AddObjectToObject(json, "fields");
  cJSON_AddNumberToObject(fields, "value", 1);
  cJSON_AddStringToObject(fields, "event_id", event_id);
  cJSON_AddNumberToObject(fields, "fill_seq", event->fill_seq);
  cJSON_AddNumberToObject(fields, "banks_run", event->banks_run);
  cJSON_AddNumberToObject(fields, "banks_today", event->banks_today);
  cJSON_AddNumberToObject(fields, "banks_total", event->banks_total);
  cJSON_AddNumberToObject(fields, "valve_number", event->valve_number);
  cJSON_AddNumberToObject(fields, "encoder_offset", event->encoder_offset);

  char *json_string = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!json_string) return ESP_FAIL;

  esp_http_client_config_t config = {};
  config.url = telemetry_url;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = 10000;
  config.skip_cert_common_name_check = true;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.cert_pem = nullptr;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(json_string);
    return ESP_FAIL;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json_string, strlen(json_string));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    const int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
      ESP_LOGE(TAG, "Telemetry HTTP status=%d for seq=%ld", status, (long)event->fill_seq);
      err = ESP_FAIL;
    } else {
      ESP_LOGI(TAG, "Telemetry sent: seq=%ld run=%ld today=%ld total=%ld",
               (long)event->fill_seq, (long)event->banks_run,
               (long)event->banks_today, (long)event->banks_total);
    }
  } else {
    ESP_LOGE(TAG, "Telemetry send failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  free(json_string);
  return err;
}

void telemetryTask(void *pvParameter) {
  telemetry_fill_event_t event = {};
  while (true) {
    if (xQueueReceive(telemetry_queue, &event, portMAX_DELAY) != pdTRUE) continue;
    if (!is_wifi_connected()) {
      ESP_LOGW(TAG, "WiFi disconnected, telemetry dropped seq=%ld", (long)event.fill_seq);
      continue;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
      const esp_err_t err = send_fill_event_http(&event);
      if (err == ESP_OK) break;
      if (attempt < 2) {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
  }
}

esp_err_t telemetry_queue_init(void) {
  if (telemetry_queue) return ESP_OK;
  telemetry_queue = xQueueCreate(16, sizeof(telemetry_fill_event_t));
  if (!telemetry_queue) return ESP_FAIL;
  return ESP_OK;
}

esp_err_t telemetry_send_fill_event_async(const telemetry_fill_event_t *event) {
  if (!event || !telemetry_queue) return ESP_ERR_INVALID_STATE;
  if (uxQueueMessagesWaiting(telemetry_queue) >= 16) {
    ESP_LOGW(TAG, "Telemetry queue full, dropping seq=%ld", (long)event->fill_seq);
    return ESP_FAIL;
  }
  return xQueueSend(telemetry_queue, event, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_FAIL;
}
