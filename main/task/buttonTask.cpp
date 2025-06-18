// Button #25 #33.
#include "driver/gpio.h"
#include "main.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static const Pintype BUTTON_PIN1 = GPIO_NUM_0;   // bottom
static const Pintype BUTTON_PIN2 = GPIO_NUM_35;  // top
static const Pintype BUTTON_STOP = GPIO_NUM_36;  // STOP
static const Pintype BUTTON_FLUSH = GPIO_NUM_37; // FLUSH
static const Pintype BUTTON_RUN = GPIO_NUM_38;   // RUN

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

#include "screenTask.h" // Добавили заголовок для screenTask
#include <buttonTask.h>

static const char *states[] = {
    [BUTTON_PRESSED] = "pressed",
    [BUTTON_RELEASED] = "released",
    [BUTTON_CLICKED] = "clicked",
    [BUTTON_PRESSED_LONG] = "pressed long",
};

static button_t btn1, btn2, btn_stop, btn_flush, btn_run;
TaskHandle_t button;
// mqttMessage eventMessage;
static void on_button(button_t *btn, button_state_t state) {
  uint32_t notify_value = 0; // Значение для уведомления
  if (state == BUTTON_PRESSED_LONG) {
    if (btn == &btn1 || btn == &btn_stop) {
      ESP_LOGW(BUTTON_TAG, "RED PRESSED LONG");
    }
  }
  if (state == BUTTON_CLICKED) {
    if (btn == &btn_stop) {
      ESP_LOGI(BUTTON_TAG, "STOP CLICK");
      xTaskNotify(screen, BTN_STOP_BIT, eSetBits);
      xTaskNotify(counter, BTN_STOP_BIT, eSetBits);
    }
    if (btn == &btn_flush) {
      ESP_LOGI(BUTTON_TAG, "FLUSH CLICK");
      xTaskNotify(screen, BTN_FLUSH_BIT, eSetBits);
      xTaskNotify(counter, BTN_FLUSH_BIT, eSetBits);
    }
    if (btn == &btn_run) {
      ESP_LOGI(BUTTON_TAG, "RUN CLICK");
      xTaskNotify(screen, BTN_RUN_BIT, eSetBits);
      xTaskNotify(counter, BTN_RUN_BIT, eSetBits);
    }
    if (btn == &btn1) {
      ESP_LOGI(BUTTON_TAG, "Btn1 CLICK");
      xTaskNotify(screen, BTN1_BUTTON_CLICKED_BIT, eSetBits);
      xTaskNotify(counter, BTN1_BUTTON_CLICKED_BIT, eSetBits);
    }
    if (btn == &btn2) {
      ESP_LOGI(BUTTON_TAG, "Btn2 CLICK");
      xTaskNotify(screen, BTN2_BUTTON_CLICKED_BIT, eSetBits);
      xTaskNotify(counter, BTN2_BUTTON_CLICKED_BIT, eSetBits);
    }
  }
  if (state == BUTTON_PRESSED) {
  }
  if (state == BUTTON_RELEASED) {
  }
  // Notify screenTask
  if (notify_value && false) {
    // Отправляем уведомление задаче screenTask
    xTaskNotify(screen, notify_value, eSetBits);
    ESP_LOGI(BUTTON_TAG, "%s button %s", btn == &btn1 ? "First" : "Second",
             states[state]);
  }
}

void buttonTask(void *pvParam) {
  // First button connected between GPIO and GND
  // pressed logic level 0, no autorepeat
  btn1.gpio = BUTTON_PIN1;
  btn1.pressed_level = 0;
  btn1.internal_pull = true;
  btn1.autorepeat = true;
  btn1.callback = on_button;

  // Second button connected between GPIO and +3.3V
  // pressed logic level 1, autorepeat enabled
  btn2.gpio = BUTTON_PIN2;
  btn2.pressed_level = 0;
  btn2.internal_pull = true;
  btn2.autorepeat = false;
  btn2.callback = on_button;

  // STOP button
  btn_stop.gpio = BUTTON_STOP;
  btn_stop.pressed_level = 0;
  btn_stop.internal_pull = true;
  btn_stop.autorepeat = true;
  btn_stop.callback = on_button;

  // FLUSH button
  btn_flush.gpio = BUTTON_FLUSH;
  btn_flush.pressed_level = 0;
  btn_flush.internal_pull = true;
  btn_flush.autorepeat = false;
  btn_flush.callback = on_button;

  // RUN button
  btn_run.gpio = BUTTON_RUN;
  btn_run.pressed_level = 0;
  btn_run.internal_pull = true;
  btn_run.autorepeat = false;
  btn_run.callback = on_button;

  ESP_ERROR_CHECK(button_init(&btn1));
  ESP_ERROR_CHECK(button_init(&btn2));
  ESP_ERROR_CHECK(button_init(&btn_stop));
  ESP_ERROR_CHECK(button_init(&btn_flush));
  ESP_ERROR_CHECK(button_init(&btn_run));

  const TickType_t xBlockTime = pdMS_TO_TICKS(9999 * 1000);
  while (true) {
    vTaskDelay(xBlockTime);
    if (true) {
      ESP_LOGW(BUTTON_TAG, "button!");
    }
  }
}
