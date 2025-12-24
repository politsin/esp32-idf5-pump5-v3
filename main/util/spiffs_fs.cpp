#include "spiffs_fs.h"

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "SPIFFS_FS";

static bool s_mounted = false;

const char *spiffs_fs_base_path(void) { return "/spiffs"; }
const char *spiffs_fs_partition_label(void) { return "storage"; }

esp_err_t spiffs_fs_mount(void) {
  if (s_mounted) {
    return ESP_OK;
  }

  esp_vfs_spiffs_conf_t conf = {};
  conf.base_path = spiffs_fs_base_path();
  conf.partition_label = spiffs_fs_partition_label();
  conf.max_files = 8;
  conf.format_if_mount_failed = false;

  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_vfs_spiffs_register failed: %s", esp_err_to_name(err));
    return err;
  }

  size_t total = 0, used = 0;
  err = esp_spiffs_info(spiffs_fs_partition_label(), &total, &used);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "SPIFFS mounted: total=%u used=%u", (unsigned)total, (unsigned)used);
  } else {
    ESP_LOGW(TAG, "esp_spiffs_info failed: %s", esp_err_to_name(err));
  }

  s_mounted = true;
  return ESP_OK;
}


