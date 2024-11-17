#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <rom/gpio.h>
#include <stdio.h>

#include "util/config.h"
#include <main.h>
#define MAINTAG "MAIN"

// tasks
#include "task/blinkTask.h"
#include "task/buttonTask.h"
#include "task/counterTask.h"
#include "task/encoderTask.h"
#include "task/hx711Task.h"
#include "task/screenTask.h"
#include "task/stepperTask.h"
#include "task/tofTask.h"
#include "task/uartTask.h"

#include "i2cdev.h"
#include "util/i2c.h"

app_state_t app_state = {
    .is_on = false,
    .time = 0,
    .encoder = 0,
    .water_target = 0,
    .water_current = 0,
    .water_delta = 0,
    .freeHeap = 0,
};

extern "C" void app_main(void) {
  config_init();
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("gpio", ESP_LOG_WARN);
  esp_log_level_set("DendoStepper", ESP_LOG_WARN);
  esp_log_level_set("BUTTON", ESP_LOG_WARN);
  // esp_log_level_set("COUNTER", ESP_LOG_WARN);
  esp_log_level_set("ENCODER", ESP_LOG_WARN);
  ESP_LOGW(MAINTAG, "Hello world!!");
  uint32_t min = 768 + configSTACK_OVERHEAD_TOTAL;

  // tasks.
  // i2c_init(true);
  ESP_ERROR_CHECK(i2cdev_init());
  xTaskCreate(&loop, "loop", min * 3, NULL, 2, NULL);
  // xTaskCreate(stepperTask, "stepper", min * 8, NULL, 1, &stepper);
  // xTaskCreate(tofTask, "tof", min * 32, NULL, 1, &tof);
  // xTaskCreate(blinkTask, "blink", min * 4, NULL, 1, &blink);
  xTaskCreate(buttonTask, "button", min * 4, NULL, 1, &button);
  xTaskCreate(encoderTask, "encoder", min * 6, NULL, 1, &encoder);
  xTaskCreate(counterTask, "counter", min * 6, NULL, 1, &counter);
  xTaskCreatePinnedToCore(screenTask, "screen", min * 8, NULL, 1, &screen, 1);
  // xTaskCreatePinnedToCore(uartTask, "uart", min * 10, NULL, 1, &uart, 0);
  // xTaskCreate(hx711Task, "hx711", min * 16, NULL, 1, &hx711);
  // xTaskCreate(i2cScanTask, "i2cScan", min * 4, NULL, 5, &i2cScan);
}

// Loop Task
void loop(void *pvParameter) {
  uint32_t size = 0;
  uint32_t count = 0;
  const TickType_t xBlockTime = pdMS_TO_TICKS(5 * 1000);
  while (1) {
    count++;
    if ((count % 100) == true) {
      size = xPortGetFreeHeapSize();
      app_state.freeHeap = size;
      // ESP_LOGW(MAINTAG, "xP %ld", size);
      vTaskDelay(xBlockTime);
    }
  }
}
