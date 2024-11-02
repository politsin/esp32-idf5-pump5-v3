#include "freertos/FreeRTOS.h"
#include <esp_log.h>

#include "esp_system.h"
#include "nvs_flash.h"

static const char *NVSTAG = "NVS";

void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      ESP_LOGE(NVSTAG, "%d nvs_flash_erase", err);
    } else {
      err = nvs_flash_init();
      if (err != ESP_OK) {
        ESP_LOGE(NVSTAG, "%d nvs_flash_init", err);
      }
    }
  }
}
