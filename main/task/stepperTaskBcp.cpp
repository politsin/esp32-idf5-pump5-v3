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

app_config_t app_config = {
    .steps = 0,
    .speed = 0,
};

DendoStepper_config_t stepConfig = {.stepPin = (uint8_t)GPIO_NUM_13,
                                    .dirPin = (uint8_t)GPIO_NUM_15,
                                    .enPin = (uint8_t)GPIO_NUM_2,
                                    .miStep = MICROSTEP_32,
                                    .stepAngle = 1.8};

DendoStepper step = DendoStepper();

TaskHandle_t stepper;
void stepperTask(void *pvParam) {
  static const TickType_t xBlockTime = pdMS_TO_TICKS(50);
  // config
  static const TickType_t xOffTime = pdMS_TO_TICKS(100);
  esp_err_t err;
  config->get_item("steps", app_config.steps);
  config->get_item("speed", app_config.speed);
  if (app_config.steps < 100) {
    app_config.steps = 10000;
  }
  if (app_config.speed < 100) {
    app_config.speed = 10000;
  }
  ESP_LOGW(STEPPER_TAG, "Steps FROM MEM: %lu", app_config.steps);
  ESP_LOGW(STEPPER_TAG, "Speed FROM MEM: %lu", app_config.speed);


  // DendoStepper init
  step.config(&stepConfig);
  step.init();
  // step.setSpeed(app_config.speed, 1000, 1000);
  step.setSpeed(1500, 500, 500);
  // step.setStepsPerMm(10);
  step.disableMotor();
  step.runInf(false);

  uint32_t notify_value;
  uint32_t mode;
  int16_t value;
  int32_t steps = app_config.steps;
  int32_t encoder;
  ESP_LOGW(STEPPER_TAG, "stepper!");
  while (true) {
    if (xTaskNotifyWait(0, 0, &notify_value, 0) == pdTRUE) {
      encoder = (int32_t)notify_value;
      if (notify_value == 5000) {
        ESP_LOGI(STEPPER_TAG, "run");
        step.enableMotor();
        // step.runPos(app_config.steps);
        step.runPos(20);
        // step.runPos(-500);
        // step.stop();
        vTaskDelay(xOffTime);
        step.disableMotor();
        ESP_LOGI(STEPPER_TAG, "done");
      }
      else if (notify_value == 5001) {
        ESP_LOGI(STEPPER_TAG, "Encoder STOP");
        step.enableMotor();
        step.stop(); 
        step.runPos(-20);
        vTaskDelay(xOffTime);
        step.disableMotor();
        // step.stop();
      } else {
        app_config.steps = steps - encoder;
        config->set_item("steps", app_config.steps);
        config->commit();
        ESP_LOGI(STEPPER_TAG, "Encoder diff %ld -> steps = %ld", encoder, app_config.steps);
      }
      // Old staff;
      mode = notify_value & 0xff;
      value = (int16_t)(notify_value >> 16);
      switch (mode) {
      case 1:
        ESP_LOGI(STEPPER_TAG, "ENC: %d", value);
        break;
      case 2:
        // ESP_LOGI(STEPPER_TAG, "ENC_BTN: %d", value);
        break;
      default:
        // ESP_LOGI(STEPPER_TAG, "enc! %d %x", value, mode);
        break;
      }
    }
    vTaskDelay(xBlockTime);
  }
}
