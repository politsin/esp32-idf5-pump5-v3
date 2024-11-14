#pragma once

#include <freertos/queue.h>
#include <freertos/task.h>
#include <iostream>
using std::string;
// #include "rotary_encoder.h"

extern TaskHandle_t encoder;
void encoderTask(void *pvParam);

void configureRmt(uint32_t freq, bool init);
void configureEncoderPins();
