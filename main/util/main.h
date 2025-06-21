#include <iostream>
using std::string;

#include "driver/gpio.h"
typedef gpio_num_t Pintype;

void mqtt_callback(string param, string value);

void loop(void *pvParameter);

#ifndef APP_MAIN_H_
#define APP_MAIN_H_

typedef struct {
  uint32_t steps;
  int32_t encoder;
} app_config_t;

typedef struct {
  bool is_on;
  bool rock;
  int32_t time;
  int32_t encoder;
  int32_t water_target;
  int32_t water_current;
  int32_t water_delta;
  int32_t freeHeap;
  int8_t valve;
  int32_t valve_times[5]; // Время налива для каждого клапана P1-P5 (в секундах)
  int32_t banks_count; // Счётчик налитых банок
  int32_t start_time; // Время старта в тиках FreeRTOS
  int32_t final_time; // Финальное время при остановке (в секундах)
  int32_t final_banks; // Финальное количество банок при остановке
  bool counter_error; // Флаг ошибки счётчика
} app_state_t;

// Bitmask for screen notifications.  Note: this is an enum, not a struct.
typedef enum {
  UPDATE_BIT = (1 << 0),
  COUNTER_START_BIT = (1 << 1),
  COUNTER_FINISHED_BIT = (1 << 2),
  BTN1_BUTTON_CLICKED_BIT = (1 << 3),
  BTN1_BUTTON_PRESSED_BIT = (1 << 4),
  BTN1_BUTTON_RELEASED_BIT = (1 << 5),
  BTN2_BUTTON_CLICKED_BIT = (1 << 6),
  BTN2_BUTTON_PRESSED_BIT = (1 << 7),
  BTN2_BUTTON_RELEASED_BIT = (1 << 8),
  BTN_STOP_BIT = (1 << 9),
  BTN_FLUSH_BIT = (1 << 10),
  BTN_RUN_BIT = (1 << 11),
  ENCODER_CHANGED_BIT = (1 << 12),
  ENCODER_BTN_BIT = (1 << 13),
} screen_notification_t;

#endif /* APP_MAIN_H_ */

extern app_state_t app_state;
extern app_config_t app_config;
// i2c::
// void icScan();
// #define SCL_GPIO 25
// #define SDA_GPIO 33


