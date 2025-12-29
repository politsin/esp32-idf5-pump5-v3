#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// GPIO входа, на который приходит счётчик тиков (датчик расхода).
// Используется и в задаче counter, и в веб-интерфейсе (для отображения).
static constexpr gpio_num_t COUNTER_TICK_GPIO = GPIO_NUM_26;

extern TaskHandle_t counter;
void counterTask(void *pvParam);

