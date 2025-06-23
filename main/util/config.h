#ifndef IOT_CONFIG_H_
#define IOT_CONFIG_H_


#include <esp_err.h>
#include <stdio.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"

esp_err_t config_init();
esp_err_t save_total_banks_count(int32_t total_banks);
esp_err_t save_today_banks_count(int32_t today_banks);
esp_err_t load_today_banks_count(int32_t* today_banks);
esp_err_t check_and_reset_daily_counter();
extern std::shared_ptr<nvs::NVSHandle> config;

#endif /* IOT_CONFIG_H_ */
