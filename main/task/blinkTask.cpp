// #include <blinkTask.h> WTF o.O
// Blink #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype LED = GPIO_NUM_22;
#include "sdkconfig.h"
#include <esp_log.h>
#include <rom/gpio.h>
#define BLINK_TAG "BLINK"

TaskHandle_t blink;
void blinkTask(void *pvParam) {
  int isOn = 0;
  gpio_pad_select_gpio(LED);
  gpio_set_direction(LED, GPIO_MODE_OUTPUT);
  gpio_set_level(LED, !isOn);
  const TickType_t xBlockTime = pdMS_TO_TICKS(3 * 1000);
  while (true) {
    gpio_set_level(LED, isOn);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(LED, !isOn);
    vTaskDelay(450 / portTICK_PERIOD_MS);
    if (false) {
      ESP_LOGW(BLINK_TAG, "blink!");
    }
    vTaskDelay(xBlockTime);
  }
}
