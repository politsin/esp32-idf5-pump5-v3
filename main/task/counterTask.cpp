// #include <counterTask.h> WTF o.O
// Counter #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype DI = GPIO_NUM_26;
static constexpr Pintype PUMP = GPIO_NUM_27;
#include "sdkconfig.h"
#include <esp_log.h>
#include <rom/gpio.h>
#include <main.h>
#include <config.h>
#define COUNTER_TAG "COUNTER"

TaskHandle_t counter;

// Объявляем счетчик как глобальную переменную
volatile uint32_t rot = 0;

// Функция обработки прерывания
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  // Увеличиваем счетчик при каждом прерывании
  rot++;
}

app_config_t app_config = {
    .steps = 1350,
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
  const TickType_t xBlockTime = pdMS_TO_TICKS(100);

  // config->get_item("steps", app_config.steps);
  config->get_item("encoder", app_config.encoder);
  ESP_LOGW(COUNTER_TAG, "Steps FROM MEM: %lu", app_config.steps);
  ESP_LOGW(COUNTER_TAG, "Enc FROM MEM: %lu", app_config.encoder);

  bool isOn = false;
  uint32_t i = 0;
  uint32_t notify_value;
  int32_t encoder = 0;
  gpio_set_level(PUMP, 0);
  while (true) {
    if (xTaskNotifyWait(0, 0, &notify_value, 0) == pdTRUE) {
      if (notify_value == 5000) {
        ESP_LOGI(COUNTER_TAG, "blue btn");
        // AI ON
        isOn = true;
        gpio_set_level(PUMP, isOn);
      } else if (notify_value == 5001) {
        ESP_LOGI(COUNTER_TAG, "red btn");
        // AI OFF
        rot = 0;
        isOn = false;
        gpio_set_level(PUMP, isOn);
      } else {
        encoder = (int32_t)notify_value;
        app_config.encoder = encoder;
        config->set_item("steps", app_config.encoder);
        config->commit();
        ESP_LOGW(COUNTER_TAG, "Encoder %ld", encoder);
      }
    }
    if (rot > app_config.steps + encoder) {
      isOn = false;
      gpio_set_level(PUMP, isOn);
      ESP_LOGI(COUNTER_TAG, "STOP %ld | done = %ld", app_config.steps + encoder, rot);
      rot = 0;
      isOn = false;
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if ((i++ % 10) == true) {
      // Выводим значение счетчика
      ESP_LOGI(COUNTER_TAG, "Counter[%d]: %ld", (int)isOn, rot);
    }
    vTaskDelay(xBlockTime);
  }
}
