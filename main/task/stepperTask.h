#pragma once

#include "driver/gpio.h"
#include <freertos/queue.h>
#include <freertos/task.h>
#include <DendoStepper.h>

extern TaskHandle_t stepper;
extern DendoStepper step;
void stepperTask(void *pvParam);
