// #include <counterTask.h> WTF o.O
// Counter #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype DI = GPIO_NUM_26;
static constexpr Pintype PUMP = GPIO_NUM_27;
#include "sdkconfig.h"
#include <config.h>
#include <esp_log.h>
#include <main.h>
#include <rom/gpio.h>
#define COUNTER_TAG "COUNTER"

#include "task/screenTask.h"
TaskHandle_t counter;

// Объявляем счетчик как глобальную переменную
volatile uint32_t rot = 0;

// Функция обработки прерывания
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  // Увеличиваем счетчик при каждом прерывании
  rot++;
}

app_config_t app_config = {
    .steps = 1210,
    .encoder = 0,
};

void counterTask(void *pvParam) {
  gpio_pad_select_gpio(DI);
  gpio_set_direction(DI, GPIO_MODE_INPUT);

  gpio_pad_select_gpio(PUMP);
  gpio_set_direction(PUMP, GPIO_MODE_OUTPUT);

  // Настраиваем прерывание по нарастающему фронту
  gpio_set_intr_type(DI, GPIO_INTR_POSEDGE);

  // Устанавливаем обработчик прерывания
  gpio_install_isr_service(0);
  gpio_isr_handler_add(DI, gpio_isr_handler, NULL);
  const TickType_t xBlockTime = pdMS_TO_TICKS(250);

  // config->get_item("steps", app_config.steps);
  config->get_item("encoder", app_config.encoder);
  ESP_LOGW(COUNTER_TAG, "Steps FROM MEM: %lu", app_config.steps);
  ESP_LOGW(COUNTER_TAG, "Enc FROM MEM: %lu", app_config.encoder);

  bool isOn = false;
  uint32_t i = 0;
  uint32_t notification;
  app_state.water_target = app_config.steps;
  gpio_set_level(PUMP, 0);
  while (true) {
    if (xTaskNotifyWait(0x0, ULONG_MAX, &notification, 0) ==
        pdTRUE) { // Wait for any notification
      if (notification & YELL_BUTTON_CLICKED_BIT) {
        ESP_LOGI(COUNTER_TAG, "Yellow button clicked");
        rot = 0;
        isOn = true;
        gpio_set_level(PUMP, isOn);
        app_state.water_delta = 0;
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
      }
      if (notification & RED_BUTTON_PRESSED_BIT) {
        ESP_LOGI(COUNTER_TAG, "Red button pressed");
        rot = 0;
        isOn = false;
        gpio_set_level(PUMP, isOn);
      }
      if (notification & ENCODER_CHANGED_BIT) {
        app_state.water_target = app_config.steps + app_state.encoder;
        app_config.encoder = app_state.encoder;
        // config->set_item("steps", app_config.encoder);
        // config->commit();
        xTaskNotify(screen, UPDATE_BIT, eSetBits);
        ESP_LOGW(COUNTER_TAG, "Encoder changed: %ld", app_state.encoder);
      }
    }
    app_state.is_on = isOn;
    app_state.water_current = rot;
    if (rot > app_state.water_target) {
      isOn = false;
      gpio_set_level(PUMP, isOn);
      ESP_LOGI(COUNTER_TAG, "STOP %ld | done = %ld", app_state.water_target,
               rot);
      isOn = false;
      app_state.is_on = isOn;
      app_state.water_delta = app_state.water_target - rot;
      // Notify screen task about counter finishing
      xTaskNotify(screen, COUNTER_FINISHED_BIT, eSetBits);
      vTaskDelay(pdMS_TO_TICKS(1000));
      rot = 0;
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if ((i++ % 30) == true) {
      // xTaskNotify(screen, UPDATE_BIT, eSetBits);
      // Выводим значение счетчика
      ESP_LOGI(COUNTER_TAG, "Counter[%d]: %ld  >>  %ld", (int)isOn, rot,
               app_state.water_target);
    }
    vTaskDelay(xBlockTime);
  }
}
