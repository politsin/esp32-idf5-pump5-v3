#pragma once

#include "esp_err.h"

// Монтирует SPIFFS раздел "storage" в /spiffs.
// Безопасно вызывать повторно.
esp_err_t spiffs_fs_mount(void);

const char *spiffs_fs_base_path(void);       // "/spiffs"
const char *spiffs_fs_partition_label(void); // "storage"


