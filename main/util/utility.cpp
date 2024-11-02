// #include "utility.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include <string>
using std::string;
#include <esp_log.h>

#define UTILTAG "UTIL"
void chip_info(void) {
  /* Print chip information */
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ", chip_info.cores,
         (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
         (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

  printf("silicon revision %d, ", chip_info.revision);

  printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
         (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                       : "external");
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
