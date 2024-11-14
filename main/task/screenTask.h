#pragma once

#include "driver/gpio.h"
#include <freertos/queue.h>
#include <freertos/task.h>

#define CONFIG_TFT_eSPI_ESPIDF 1
#define CONFIG_LILYGO_T_DISPLAY 1

extern TaskHandle_t screen;
void screenTask(void *pvParam);
