#pragma once

#include "esp_err.h"

// Монтирует SPIFFS раздел "storage" в /spiffs.
// Безопасно вызывать повторно.
esp_err_t spiffs_fs_mount(void);

// Размонтирует SPIFFS (storage). Нужен для безопасной перезаписи партиции (например OTA web assets).
// Безопасно вызывать повторно.
esp_err_t spiffs_fs_unmount(void);

const char *spiffs_fs_base_path(void);       // "/spiffs"
const char *spiffs_fs_partition_label(void); // "storage"


