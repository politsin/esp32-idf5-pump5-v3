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

#include "button.h"
#include <esp_log.h>
#define BUTTON_TAG "BUTTON"

typedef gpio_num_t Pintype;
#include "counterTask.h"
#include "freertos/task.h"
#include "main.h"
// esp_idf_lib_helpers.h
// #include "mqttTask.h"

#include "screenTask.h" // Добавили заголовок для screenTask
#include <buttonTask.h>
#include "../util/telegram_manager.h"
#include "../util/pcf8575_io.h"

static const char *states[] = {
    [BUTTON_PRESSED] = "pressed",
    [BUTTON_RELEASED] = "released",
    [BUTTON_CLICKED] = "clicked",
    [BUTTON_PRESSED_LONG] = "pressed long",
};

static button_t btn1, btn2, btn_stop, btn_flush, btn_run;
TaskHandle_t button;
// mqttMessage eventMessage;
static SemaphoreHandle_t pcf_int_sem;
static bool last_stop = false, last_flush = false, last_run = false;
static const int ENC_STEP_CLICK = 1;  // шаг по клику
static const int ENC_STEP_LONG  = 10; // шаг по долгому удержанию
static volatile bool buttons_enabled = false; // защита от «корёжит» сразу после старта
static volatile TickType_t next_repeat1 = 0, next_repeat2 = 0;
static volatile bool repeat_session1 = false, repeat_session2 = false;
static volatile TickType_t repeat_deadline1 = 0, repeat_deadline2 = 0;

static void IRAM_ATTR pcf_int_isr(void* arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (pcf_int_sem) xSemaphoreGiveFromISR(pcf_int_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}
static void on_button(button_t *btn, button_state_t state) {
  // Игнорируем события до разрешения кнопок (стартовая стабилизация)
  if (!buttons_enabled && (btn == &btn1 || btn == &btn2)) {
    return;
  }
  uint32_t notify_value = 0; // Значение для уведомления
  if (state == BUTTON_PRESSED_LONG) {
    // Долгое нажатие: шаг ±10; с включённым autorepeat библиотека будет присылать повторяющиеся LONG-события
    if (btn == &btn1) {
      app_state.encoder -= 10;
      xTaskNotify(screen, ENCODER_CHANGED_BIT, eSetBits);
      xTaskNotify(counter, ENCODER_CHANGED_BIT, eSetBits);
      ESP_LOGI(BUTTON_TAG, "Encoder shift long -= 10 -> %ld", app_state.encoder);
    } else if (btn == &btn2) {
      app_state.encoder += 10;
      xTaskNotify(screen, ENCODER_CHANGED_BIT, eSetBits);
      xTaskNotify(counter, ENCODER_CHANGED_BIT, eSetBits);
      ESP_LOGI(BUTTON_TAG, "Encoder shift long += 10 -> %ld", app_state.encoder);
    }
  }
  if (state == BUTTON_CLICKED) {
    if (btn == &btn_stop) {
      ESP_LOGI(BUTTON_TAG, "STOP CLICK");
      xTaskNotify(screen, BTN_STOP_BIT, eSetBits);
      xTaskNotify(counter, BTN_STOP_BIT, eSetBits);
      // Отчёт с информацией о банках отправляется в counterTask
    }
    if (btn == &btn_flush) {
      ESP_LOGI(BUTTON_TAG, "FLUSH CLICK");
      xTaskNotify(screen, BTN_FLUSH_BIT, eSetBits);
      xTaskNotify(counter, BTN_FLUSH_BIT, eSetBits);
      telegram_send_button_press_with_icon("🚿", "FLUSH");
    }
    if (btn == &btn_run) {
      ESP_LOGI(BUTTON_TAG, "RUN CLICK");
      xTaskNotify(screen, BTN_RUN_BIT, eSetBits);
      xTaskNotify(counter, BTN_RUN_BIT, eSetBits);
      telegram_send_button_press_with_icon("🟢", "START");
    }
    // Кнопки платы: клик ±1
    if (btn == &btn1) {
      app_state.encoder -= 1;
      xTaskNotify(screen, ENCODER_CHANGED_BIT, eSetBits);
      xTaskNotify(counter, ENCODER_CHANGED_BIT, eSetBits);
      ESP_LOGI(BUTTON_TAG, "Encoder shift -= 1 -> %ld", app_state.encoder);
    }
    if (btn == &btn2) {
      app_state.encoder += 1;
      xTaskNotify(screen, ENCODER_CHANGED_BIT, eSetBits);
      xTaskNotify(counter, ENCODER_CHANGED_BIT, eSetBits);
      ESP_LOGI(BUTTON_TAG, "Encoder shift += 1 -> %ld", app_state.encoder);
    }
  }
  if (state == BUTTON_PRESSED) { /* no-op */ }
  if (state == BUTTON_RELEASED) { /* no-op */ }
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

  // Second button connected between GPIO and GND (активный низ)
  // pressed logic level 0, autorepeat disabled
  btn2.gpio = BUTTON_PIN2;
  btn2.pressed_level = 0;
  btn2.internal_pull = false; // GPIO35: нет внутренних подтяжек
  btn2.autorepeat = true;
  btn2.callback = on_button;

  // STOP/FLUSH/RUN теперь читаем через PCF8575 (виртуальные кнопки)
  btn_stop.callback = on_button;
  btn_flush.callback = on_button;
  btn_run.callback = on_button;

  ESP_ERROR_CHECK(button_init(&btn1));
  ESP_ERROR_CHECK(button_init(&btn2));
  // Не инициализируем btn_stop/btn_flush/btn_run через GPIO-библиотеку

  // Настраиваем прерывание от PCF8575 INT
  gpio_install_isr_service(0);
  pcf_int_sem = xSemaphoreCreateBinary();
  gpio_reset_pin(PCF8575_INT_GPIO);
  gpio_set_direction(PCF8575_INT_GPIO, GPIO_MODE_INPUT);
  gpio_set_intr_type(PCF8575_INT_GPIO, GPIO_INTR_NEGEDGE);
  gpio_isr_handler_add(PCF8575_INT_GPIO, pcf_int_isr, NULL);

  const TickType_t xBlockTime = pdMS_TO_TICKS(50);
  // Разрешим обработку кнопок через короткую паузу после старта,
  // чтобы входы успели стабилизироваться
  TickType_t enable_at = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
  // Ручного опроса и репита больше нет — используем библиотеку
  while (true) {
    if (!buttons_enabled && xTaskGetTickCount() >= enable_at) {
      buttons_enabled = true;
    }
    // Ждём событие от INT или таймаут (для страховки/дребезга)
    if (pcf_int_sem) (void)xSemaphoreTake(pcf_int_sem, xBlockTime);
    // Небольшая задержка для подавления дребезга
    vTaskDelay(pdMS_TO_TICKS(10));

    bool stop_p = false, flush_p = false, run_p = false;
    if (ioexp_read_buttons(&stop_p, &flush_p, &run_p) == ESP_OK) {
      // Генерация CLICK по фронту нажатия (релиз->нажатие)
      if (stop_p && !last_stop)  on_button(&btn_stop, BUTTON_CLICKED);
      if (flush_p && !last_flush) on_button(&btn_flush, BUTTON_CLICKED);
      if (run_p && !last_run)    on_button(&btn_run, BUTTON_CLICKED);
      last_stop = stop_p;
      last_flush = flush_p;
      last_run = run_p;
    }
  }
}
