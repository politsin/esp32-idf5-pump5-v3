// #include "utility.h"
#include <stdint.h>
#include <inttypes.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include <string>
using std::string;
#include <esp_log.h>

#define UTILTAG "UTIL"
void chip_info(void) {
  /* Print chip information */
  esp_chip_info_t info;
  esp_chip_info(&info);
  printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ", info.cores,
         (info.features & CHIP_FEATURE_BT) ? "/BT" : "",
         (info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

  printf("silicon revision %d, ", info.revision);

  uint32_t flash_size_bytes = 0;
  // NULL => esp_flash_default_chip
  (void)esp_flash_get_physical_size(nullptr, &flash_size_bytes);
  const uint32_t flash_mb = (uint32_t)(flash_size_bytes / (1024UL * 1024UL));
  printf("%" PRIu32 "MB %s flash\n", flash_mb,
         (info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

void restart(void) {
  for (int i = 25; i >= 0; i--) {
    ESP_LOGW(UTILTAG, "Restarting in %d seconds...", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  ESP_LOGE(UTILTAG, "Restarting now.");
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  fflush(stdout);
  esp_restart();
}
