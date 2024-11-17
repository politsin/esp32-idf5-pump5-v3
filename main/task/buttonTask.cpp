// Button #25 #33.
#include "driver/gpio.h"
#include "main.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static const Pintype BUTTON_PIN1 = GPIO_NUM_38;
static const Pintype BUTTON_PIN2 = GPIO_NUM_37;

#include <button.h>
#include <esp_log.h>
#define BUTTON_TAG "BUTTON"

typedef gpio_num_t Pintype;
#include "counterTask.h"
#include "freertos/task.h"
#include "main.h"
#include "stepperTask.h"
// esp_idf_lib_helpers.h
// #include "mqttTask.h"

#include <buttonTask.h>
#include "screenTask.h" // Добавили заголовок для screenTask

static const char *states[] = {
    [BUTTON_PRESSED] = "pressed",
    [BUTTON_RELEASED] = "released",
    [BUTTON_CLICKED] = "clicked",
    [BUTTON_PRESSED_LONG] = "pressed long",
};

static button_t btn1, btn2;
TaskHandle_t button;
// mqttMessage eventMessage;
static void on_button(button_t *btn, button_state_t state) {
  uint32_t notify_value = 0; // Значение для уведомления
  if (state == BUTTON_PRESSED_LONG) {
    if (btn == &btn1) {
      ESP_LOGI(BUTTON_TAG, "RED PRESSED LONG");
      notify_value = RED_BUTTON_LONG_PRESSED_BIT;
      // xTaskNotify(mcp23x17, -1, eSetValueWithOverwrite);
    }
  }
  if (state == BUTTON_CLICKED) {
    if (btn == &btn2) {
      ESP_LOGI(BUTTON_TAG, "BLUE CLICK");
      // xTaskNotify(counter, 5000, eSetValueWithOverwrite);
      xTaskNotify(screen, 5000, eSetValueWithOverwrite);
      notify_value = BLUE_BUTTON_CLICKED_BIT;
      // xTaskNotify(stepper, 5000, eSetValueWithOverwrite);
    }
  }
  if (state == BUTTON_PRESSED) {
    if (btn == &btn1) {
      ESP_LOGI(BUTTON_TAG, "RED PRESSED");
      // xTaskNotify(counter, 5001, eSetValueWithOverwrite);
      xTaskNotify(screen, 5001, eSetValueWithOverwrite);
      notify_value = RED_BUTTON_PRESSED_BIT;
      // xTaskNotify(stepper, 5001, eSetValueWithOverwrite);
    }
    if (btn == &btn2) {
      ESP_LOGI(BUTTON_TAG, "BLUE PRESSED");
      notify_value = BLUE_BUTTON_PRESSED_BIT;
      // xTaskNotify(hx711, 1, eSetValueWithOverwrite);
    }
  }
  if (state == BUTTON_RELEASED) {
    if (btn == &btn1) {
      ESP_LOGI(BUTTON_TAG, "RED RELEASED");
      notify_value = RED_BUTTON_RELEASED_BIT;
      // xTaskNotify(mcp23x17, -0, eSetValueWithOverwrite);
    }
    if (btn == &btn2) {
      ESP_LOGI(BUTTON_TAG, "BLUE RELEASED");
      notify_value = BLUE_BUTTON_RELEASED_BIT;
      // xTaskNotify(hx711, 0, eSetValueWithOverwrite);
    }
  }
  // Отправляем уведомление задаче screenTask
  // Notify screenTask
  if (notify_value) {
    xTaskNotify(screen, notify_value, eSetBits);
  }
  ESP_LOGI(BUTTON_TAG, "%s button %s", btn == &btn1 ? "First" : "Second",
           states[state]);
}
void buttonTask(void *pvParam) {
  // First button connected between GPIO and GND
  // pressed logic level 0, no autorepeat
  btn1.gpio = BUTTON_PIN1;
  btn1.pressed_level = 0;
  btn1.internal_pull = true;
  btn1.autorepeat = false;
  btn1.callback = on_button;

  // Second button connected between GPIO and +3.3V
  // pressed logic level 1, autorepeat enabled
  btn2.gpio = BUTTON_PIN2;
  btn2.pressed_level = 0;
  btn2.internal_pull = true;
  btn2.autorepeat = true;
  btn2.callback = on_button;

  ESP_ERROR_CHECK(button_init(&btn1));
  ESP_ERROR_CHECK(button_init(&btn2));
  while (true) {
    vTaskDelay(99999 / portTICK_PERIOD_MS);
    if (false) {
      ESP_LOGW(BUTTON_TAG, "button!");
    }
  }
}
