#pragma once

#include "esp_err.h"

// Инициализация WiFi
esp_err_t wifi_init(void);

// OTA обновление
esp_err_t ota_update(const char* url); 
