// #include <stepperTask.h> WTF o.O
// stepper #22 #19.
#include "DendoStepper.h"
// #include "configTask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include <esp_err.h>
#include <esp_log.h>
#include <hal/gpio_types.h>
#include <hal/uart_types.h>
#include <rom/gpio.h>
#include <stdio.h>

#include "main.h"
#include "util/config.h"
#include <iostream>
using std::string;
using std::to_string;
#define STEPPER_TAG "STEPPER"


DendoStepper_config_t stepConfig = {.stepPin = (uint8_t)GPIO_NUM_13,
                                    .dirPin = (uint8_t)GPIO_NUM_15,
                                    .enPin = (uint8_t)GPIO_NUM_2,
                                    .miStep = MICROSTEP_32,
                                    .stepAngle = 1.8};

DendoStepper step = DendoStepper();

TaskHandle_t stepper;
void stepperTask(void *pvParam) {
  static const TickType_t xBlockTime = pdMS_TO_TICKS(50);
  // DendoStepper init
  uint32_t speed = 3000;
  step.config(&stepConfig);
  step.init();
  step.setSpeed(speed, 200, 200);
  step.disableMotor();
  // step.runInf(false);

  uint32_t notify_value;
  int32_t encoder = 0;
  ESP_LOGW(STEPPER_TAG, "stepper!");
  static const TickType_t xOffTime = pdMS_TO_TICKS(250);
  int32_t steps = 200 * 16;
  while (true) {
    if (xTaskNotifyWait(0, 0, &notify_value, 0) == pdTRUE) {
      if (notify_value == 5000) {
        ESP_LOGW(STEPPER_TAG, "Btn RUN %ld", steps);
        step.enableMotor();
        step.runPos(steps);
        vTaskDelay(xOffTime + pdMS_TO_TICKS(steps * 1000 / speed));
        step.disableMotor();
        ESP_LOGI(STEPPER_TAG, "done");
      }
      else if (notify_value == 5001) {
        ESP_LOGW(STEPPER_TAG, "Btn STOP %ld", steps);
        step.enableMotor();
        step.stop();
        step.runPos(-steps /4);
        vTaskDelay(xOffTime + pdMS_TO_TICKS(steps * 1000 / speed));
        step.disableMotor();
        // step.stop();
      } else {
        encoder = (int32_t)notify_value;
        ESP_LOGW(STEPPER_TAG, "Encoder %ld", encoder);
      }
    }
    vTaskDelay(xBlockTime);
  }
}
