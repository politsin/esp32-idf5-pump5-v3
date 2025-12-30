#ifndef IOT_CONFIG_H_
#define IOT_CONFIG_H_


#include <esp_err.h>
#include <stdio.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"
#include "main.h"

esp_err_t config_init();
esp_err_t save_total_banks_count(int32_t total_banks);
esp_err_t save_today_banks_count(int32_t today_banks);
esp_err_t load_today_banks_count(int32_t* today_banks);
esp_err_t check_and_reset_daily_counter();

// Настройки уставок (NVS):
// - steps: цель налива в тиках (база)
// - encoder: смещение в тиках (добавляется к steps)
// - flush_valve_ms: длительность открытия одного клапана при FLUSH (мс)
// - flush_all_ms: длительность открытия всех клапанов при FLUSH (мс)
//
// config_init() загружает значения в кэш. Для доступа используйте config_get_cached_pump_settings().
esp_err_t config_load_pump_settings(int32_t *steps, int32_t *encoder, int32_t *flush_valve_ms, int32_t *flush_all_ms);
esp_err_t config_save_pump_settings(int32_t steps, int32_t encoder, int32_t flush_valve_ms, int32_t flush_all_ms);
void config_get_cached_pump_settings(int32_t *steps, int32_t *encoder, int32_t *flush_valve_ms, int32_t *flush_all_ms);

// Индивидуальные сдвиги уставки по клапанам (в тиках): target_i = steps + encoder + valve_offset[i]
void config_get_cached_valve_offsets(int32_t valve_offset[NUM_VALVES]);
esp_err_t config_save_valve_offsets(const int32_t valve_offset[NUM_VALVES]);

// Защита от "сухого хода": помпа работает, но счётчик почти не растёт.
// - dry_run_timeout_ms: окно времени для проверки (мс)
// - dry_run_min_ticks: минимальный прирост тиков за это окно
void config_get_cached_dry_run(int32_t *dry_run_timeout_ms, int32_t *dry_run_min_ticks);

// Настройки счётчика тиков (DI):
// - tick_source: 0 = PCNT (аппаратный счётчик), 1 = GPIO ISR + debounce
// - tick_min_interval_us: минимальный интервал между импульсами (мкс) для debounce (только для GPIO ISR)
// - tick_pull: 0 = OFF, 1 = PULL-UP, 2 = PULL-DOWN
void config_get_cached_tick_counter(int32_t *tick_source, int32_t *tick_min_interval_us, int32_t *tick_pull);

extern std::shared_ptr<nvs::NVSHandle> config;

#endif /* IOT_CONFIG_H_ */
