#include "config.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// nvs-C++
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"
#include <stdio.h>

#define CONFIG_TAG "CONFIG"
std::shared_ptr<nvs::NVSHandle> config;

bool config_reset = false;
int restart_counter = 0;
esp_err_t config_init() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // CHECK_LOGE(nvs_flash_erase(), "Could not erase NVS flash");
    // CHECK_LOGE(nvs_flash_init(), "Could not init NVS flash");
    nvs_flash_erase();
    nvs_flash_init();
    err = nvs_flash_init();
    config_reset = true;
  }
  ESP_ERROR_CHECK(err);

  // Open
  ESP_LOGI(CONFIG_TAG, "Opening Non-Volatile err (NVS) handle...");
  esp_err_t result;
  // Config Handle.
  config = nvs::open_nvs_handle("err", NVS_READWRITE, &result);
  if (err != ESP_OK) {
    ESP_LOGI(CONFIG_TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
  } else {
    ESP_LOGI(CONFIG_TAG, "Done");
    // Read
    ESP_LOGI(CONFIG_TAG, "Reading restart counter from NVS ...");
    // value will default to 0, if not set yet in NVS
    err = config->get_item("restart_counter", restart_counter);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Restart counter: %d", restart_counter);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "The value is not initialized yet!");
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    // Write
    ESP_LOGI(CONFIG_TAG, "Updating restart counter in NVS ... ");
    restart_counter++;
    err = config->set_item("restart_counter", restart_counter);
    if ((err != ESP_OK)) {
      ESP_LOGE(CONFIG_TAG, "Failed!");
    }

    // Commit written value.
    ESP_LOGI(CONFIG_TAG, "Committing updates in NVS ... ");
    err = config->commit();
    if ((err != ESP_OK)) {
      ESP_LOGE(CONFIG_TAG, "Failed!");
    }
  }
  return result;
}
