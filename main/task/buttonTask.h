#pragma once

#include "driver/gpio.h"
#include <freertos/queue.h>
#include <freertos/task.h>

extern TaskHandle_t button;


#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

void buttonTask(void *pvParam);

#endif
