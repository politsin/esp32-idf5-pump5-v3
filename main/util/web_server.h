#pragma once

#include "esp_err.h"

// Простой HTTP сервер для управления устройством + OTA + web-лог.
// Запуск безопасно вызывать повторно.
esp_err_t web_server_start(void);


