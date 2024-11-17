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
  int32_t encoder;
  int32_t water_target;
  int32_t water_current;
  int32_t water_delta;
  int32_t freeHeap;
} app_state_t;

// Bitmask for screen notifications.  Note: this is an enum, not a struct.
typedef enum {
  UPDATE_BIT = (1 << 0),
  COUNTER_START_BIT = (1 << 1),
  COUNTER_FINISHED_BIT = (1 << 2),
  RED_BUTTON_PRESSED_BIT = (1 << 3),
  RED_BUTTON_RELEASED_BIT = (1 << 5),
  RED_BUTTON_LONG_PRESSED_BIT = (1 << 5),
  YELL_BUTTON_PRESSED_BIT = (1 << 6),
  YELL_BUTTON_RELEASED_BIT = (1 << 7),
  YELL_BUTTON_CLICKED_BIT = (1 << 8),
  ENCODER_CHANGED_BIT = (1 << 9),
} screen_notification_t;

#endif /* APP_MAIN_H_ */

extern app_state_t app_state;
extern app_config_t app_config;
// i2c::
// void icScan();
// #define SCL_GPIO 25
// #define SDA_GPIO 33


