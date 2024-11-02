#pragma once

#include "driver/gpio.h"
#include <freertos/queue.h>
#include <freertos/task.h>

extern TaskHandle_t blink;
void blinkTask(void *pvParam);

// ReConfig.
#ifndef CONFIG_BLINK_CONSOLE
#define CONFIG_BLINK_CONSOLE false
#endif
