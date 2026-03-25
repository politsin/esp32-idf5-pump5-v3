#include "telemetry_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

#include "config.h"
#include "task/telemetryTask.h"

static const char *TAG = "TELEMETRY_MGR";
static char s_resolved_device_id[64] = {0};

static void trim_copy(const char *src, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0) return;
  dst[0] = '\0';
  if (!src) return;

  while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
  size_t len = strlen(src);
  while (len > 0) {
    const char c = src[len - 1];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    len--;
  }
  if (len >= dst_len) len = dst_len - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void build_default_device_id(char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    snprintf(out, out_len, "esp32-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }
  snprintf(out, out_len, "esp32-unknown");
}

static void refresh_resolved_device_id() {
  char device_name[64] = {0};
  config_get_cached_telemetry(device_name, sizeof(device_name), nullptr, 0);
  trim_copy(device_name, s_resolved_device_id, sizeof(s_resolved_device_id));
  if (s_resolved_device_id[0] == '\0') {
    build_default_device_id(s_resolved_device_id, sizeof(s_resolved_device_id));
  }
}

esp_err_t telemetry_init(void) {
  ESP_LOGI(TAG, "Initializing telemetry manager...");
  refresh_resolved_device_id();
  ESP_LOGI(TAG, "Telemetry device id: %s", s_resolved_device_id);
  return telemetry_queue_init();
}

esp_err_t telemetry_send_fill_event(int32_t banks_run,
                                    int32_t banks_today,
                                    int32_t banks_total,
                                    int32_t valve_number) {
  telemetry_fill_event_t event = {};
  int32_t encoder_offset = 0;
  config_get_cached_pump_settings(nullptr, &encoder_offset, nullptr, nullptr);
  event.fill_seq = banks_total;
  event.banks_run = banks_run;
  event.banks_today = banks_today;
  event.banks_total = banks_total;
  event.valve_number = valve_number;
  event.encoder_offset = encoder_offset;
  return telemetry_send_fill_event_async(&event);
}

const char *telemetry_resolved_device_id(void) {
  refresh_resolved_device_id();
  return s_resolved_device_id;
}
