#ifndef IOT_CONFIG_H_
#define IOT_CONFIG_H_


#include <esp_err.h>
#include <stdio.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"

esp_err_t config_init();
extern std::shared_ptr<nvs::NVSHandle> config;

#endif /* IOT_CONFIG_H_ */
