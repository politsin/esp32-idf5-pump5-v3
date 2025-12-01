#pragma once

#include <esp_err.h>
#include <driver/gpio.h>
#include <stdint.h>
#include <stdbool.h>

// Кнопки/клапаны на PCF8575 (по новой разводке):
// P10..P13 -> VALVE1..VALVE4 (выходы, активный уровень 1)
// P15      -> PUMP (выход, активный уровень 1)
// P1..P3   -> STOP, FLUSH, RUN (входы, активный низ, удерживаются на '1')
//
// INT пин расширителя подключаем к GPIO_NUM_17 (можно поменять при необходимости)
#ifndef PCF8575_INT_GPIO
#define PCF8575_INT_GPIO GPIO_NUM_17
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация PCF8575 и внутреннего состояния портов
esp_err_t ioexp_init(void);

// Установить уровень клапана (1..4), level: true=включить (1), false=выключить (0)
esp_err_t ioexp_set_valve(int valve_index_1_based, bool level);

// Установить уровень всех клапанов разом
esp_err_t ioexp_set_all_valves(bool level);

// Управление помпой (бит P15)
esp_err_t ioexp_set_pump(bool level);

// Прочитать состояния кнопок; pressed=true если на входе низкий уровень
esp_err_t ioexp_read_buttons(bool* stop_pressed, bool* flush_pressed, bool* run_pressed);

#ifdef __cplusplus
}
#endif


