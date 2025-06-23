#include "config.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "time.h"

// nvs-C++
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"
#include <stdio.h>
#include <main.h>

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

    // Загружаем общий счётчик банок
    ESP_LOGI(CONFIG_TAG, "Reading total banks counter from NVS ...");
    err = config->get_item("total_banks", app_state.total_banks_count);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Total banks counter: %ld", app_state.total_banks_count);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "Total banks counter is not initialized yet!");
      app_state.total_banks_count = 0;
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading total banks counter!", esp_err_to_name(err));
      app_state.total_banks_count = 0;
    }

    // Загружаем дневной счётчик банок
    ESP_LOGI(CONFIG_TAG, "Reading today banks counter from NVS ...");
    err = config->get_item("today_banks", app_state.today_banks_count);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Today banks counter: %ld", app_state.today_banks_count);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "Today banks counter is not initialized yet!");
      app_state.today_banks_count = 0;
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading today banks counter!", esp_err_to_name(err));
      app_state.today_banks_count = 0;
    }

    // Проверяем и сбрасываем дневной счётчик если нужно
    check_and_reset_daily_counter();

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

esp_err_t save_total_banks_count(int32_t total_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(CONFIG_TAG, "Saving total banks counter to NVS: %ld", total_banks);
  esp_err_t err = config->set_item("total_banks", total_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to save total banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  // Commit written value
  err = config->commit();
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to commit total banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Total banks counter saved successfully");
  return ESP_OK;
}

esp_err_t save_today_banks_count(int32_t today_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(CONFIG_TAG, "Saving today banks counter to NVS: %ld", today_banks);
  esp_err_t err = config->set_item("today_banks", today_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to save today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  // Commit written value
  err = config->commit();
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to commit today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Today banks counter saved successfully");
  return ESP_OK;
}

esp_err_t load_today_banks_count(int32_t* today_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  esp_err_t err = config->get_item("today_banks", *today_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to load today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Today banks counter loaded: %ld", *today_banks);
  return ESP_OK;
}

esp_err_t check_and_reset_daily_counter() {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Получаем текущее время
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  // Загружаем дату последнего сброса
  int32_t last_reset_day = 0;
  esp_err_t err = config->get_item("last_reset_day", last_reset_day);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Первый запуск - сохраняем текущий день
    last_reset_day = timeinfo.tm_yday;
    config->set_item("last_reset_day", last_reset_day);
    config->commit();
    ESP_LOGI(CONFIG_TAG, "First run - setting last reset day to %ld", last_reset_day);
    return ESP_OK;
  }
  
  // Проверяем, нужно ли сбросить счётчик (новый день)
  if (timeinfo.tm_yday != last_reset_day) {
    ESP_LOGI(CONFIG_TAG, "New day detected! Resetting daily counter. Old day: %ld, New day: %d", 
             last_reset_day, timeinfo.tm_yday);
    
    // Сбрасываем дневной счётчик
    app_state.today_banks_count = 0;
    save_today_banks_count(0);
    
    // Обновляем дату последнего сброса
    config->set_item("last_reset_day", timeinfo.tm_yday);
    config->commit();
    
    ESP_LOGI(CONFIG_TAG, "Daily counter reset successfully");
  } else {
    ESP_LOGI(CONFIG_TAG, "Same day - no reset needed. Current day: %d", timeinfo.tm_yday);
  }
  
  return ESP_OK;
}
