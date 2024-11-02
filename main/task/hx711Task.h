#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// Tasak.
#include <hx711.h>
extern TaskHandle_t hx711;
void hx711Task(void *pvParam);
int32_t scale(hx711_t dev);
