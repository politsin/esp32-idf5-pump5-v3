// #include <counterTask.h> WTF o.O
// Counter #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype DI = GPIO_NUM_13;
static constexpr Pintype PUMP = GPIO_NUM_25;
static constexpr Pintype VALVE1 = GPIO_NUM_17;
static constexpr Pintype VALVE2 = GPIO_NUM_22;
static constexpr Pintype VALVE3 = GPIO_NUM_21;
static constexpr Pintype VALVE4 = GPIO_NUM_26;
static constexpr Pintype VALVE5 = GPIO_NUM_27;
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

volatile bool pumpOn = false;
volatile bool valve1On = false;
volatile bool valve2On = false;
volatile bool valve3On = false;
volatile bool valve4On = false;
volatile bool valve5On = false;

// Функция обработки прерывания
static void IRAM_ATTR counter_isr_handler(void *arg) {
  // Увеличиваем счетчик при каждом прерывании
  rot = rot + 1;
  app_state.water_current = rot;
  if (pumpOn) {
    int32_t target = app_state.water_target;
    if (rot < target) {
      gpio_set_level(VALVE1, 1);
      app_state.valve = 1;
    } else if (rot > target && rot < target * 2) {
      gpio_set_level(VALVE1, 0);
      gpio_set_level(VALVE2, 1);
      app_state.valve = 2;
    } else if (rot > target * 2 && rot < target * 3) {
      gpio_set_level(VALVE2, 0);
      gpio_set_level(VALVE3, 1);
      app_state.valve = 3;
    } else if (rot > target * 3 && rot < target * 4) {
      gpio_set_level(VALVE3, 0);
      gpio_set_level(VALVE4, 1);
      app_state.valve = 4;
    } else if (rot > target * 3 && rot < target * 5) {
      gpio_set_level(VALVE4, 0);
      gpio_set_level(VALVE5, 1);
      app_state.valve = 5;
    } else if (rot > target * 5) {
      if (app_state.rock) {
        rot = 0;
        gpio_set_level(VALVE5, 0);
        gpio_set_level(VALVE1, 1);
      } else {
        gpio_set_level(VALVE5, 0);
        gpio_set_level(PUMP, 0);
        app_state.valve = 0;
        pumpOn = false;
      }
    }
  }
}

app_config_t app_config = {
    .steps = 1050,
    .encoder = 0,
};

void counterTask(void *pvParam) {

  gpio_pad_select_gpio(PUMP);
  gpio_set_direction(PUMP, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(VALVE1);
  gpio_set_direction(VALVE1, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(VALVE2);
  gpio_set_direction(VALVE2, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(VALVE3);
  gpio_set_direction(VALVE3, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(VALVE4);
  gpio_set_direction(VALVE4, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(VALVE5);
  gpio_set_direction(VALVE5, GPIO_MODE_OUTPUT);
  gpio_set_level(PUMP, 0);
  gpio_set_level(VALVE1, 0);
  gpio_set_level(VALVE2, 0);
  gpio_set_level(VALVE3, 0);
  gpio_set_level(VALVE4, 0);
  gpio_set_level(VALVE5, 0);

  // Verify that the GPIO ISR service is installed
  gpio_install_isr_service(0);
  // DI Настраиваем прерывания
  gpio_pad_select_gpio(DI);
  gpio_set_direction(DI, GPIO_MODE_INPUT);
  gpio_set_intr_type(DI, GPIO_INTR_ANYEDGE);
  // Устанавливаем обработчик прерывания
  gpio_isr_handler_add(DI, counter_isr_handler, NULL);
  const TickType_t xBlockTime = pdMS_TO_TICKS(50);

  // config->get_item("steps", app_config.steps);
  config->get_item("encoder", app_config.encoder);
  ESP_LOGW(COUNTER_TAG, "Steps FROM MEM: %lu", app_config.steps);
  ESP_LOGW(COUNTER_TAG, "Enc FROM MEM: %lu", app_config.encoder);

  // Variables for timing
  TickType_t startTime = 0;

  bool isOn = false;
  uint32_t i = 0;
  uint32_t notification;
  app_state.water_target = app_config.steps;

  gpio_config_t di_config = {
      .pin_bit_mask = (1ULL << DI),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_ANYEDGE
  };
  gpio_config(&di_config);

  while (true) {
    if (xTaskNotifyWait(0x0, ULONG_MAX, &notification, 0) ==
        pdTRUE) { // Wait for any notification
      if (notification & BTN_FLUSH_BIT) {
        gpio_set_level(PUMP, 1);
        gpio_set_level(VALVE1, 1);
        gpio_set_level(VALVE2, 1);
        gpio_set_level(VALVE3, 1);
        gpio_set_level(VALVE4, 1);
        gpio_set_level(VALVE5, 1);
        ESP_LOGW(COUNTER_TAG, "Flush!");
      }
      if (notification & BTN_RUN_BIT) {
        app_state.rock = true;
        ESP_LOGW(COUNTER_TAG, "Run!");
        rot = 0;
        isOn = true;
        pumpOn = true;
        gpio_set_level(PUMP, isOn);
        app_state.water_delta = 0;
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(VALVE1, isOn);
        app_state.valve = 1;
      }
      if (notification & BTN_FLUSH_BIT) {
        ESP_LOGW(COUNTER_TAG, "START!! Flush");
        // Start timing
        startTime = xTaskGetTickCount();
        rot = 0;
        isOn = true;
        pumpOn = true;
        gpio_set_level(PUMP, isOn);
        app_state.water_delta = 0;
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(VALVE1, isOn);
        app_state.valve = 1;
      }
      if (notification & BTN_STOP_BIT) {
        ESP_LOGW(COUNTER_TAG, "STOP Emegrensy!");
        app_state.rock = false;
        rot = 0;
        isOn = false;
        pumpOn = false;
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 0);
        app_state.valve = 0;
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(PUMP, 0);
      }
      if (notification & ENCODER_CHANGED_BIT) {
        app_state.water_target = app_config.steps + app_state.encoder;
        // app_config.encoder = app_state.encoder;
        // config->set_item("steps", app_config.encoder);
        // config->commit();
        xTaskNotify(screen, UPDATE_BIT, eSetBits);
        ESP_LOGW(COUNTER_TAG, "Encoder changed: %ld", app_state.encoder);
      }
    }
    app_state.is_on = isOn;
    app_state.water_current = rot;
    if (!app_state.rock && rot > app_state.water_target * 5) {
      isOn = false;
      gpio_set_level(PUMP, isOn);

      // Stop timing and calculate elapsed time
      TickType_t endTime = xTaskGetTickCount();
      app_state.time = (endTime - startTime) / 100; // Convert ticks to seconds
      ESP_LOGW(COUNTER_TAG, "STOP %ld | done = %ld| Time: %ld seconds",
               app_state.water_target, rot, app_state.time);

      isOn = false;
      app_state.is_on = isOn;
      app_state.water_delta = rot - app_state.water_target * 5;
      // Notify screen task about counter finishing
      xTaskNotify(screen, COUNTER_FINISHED_BIT, eSetBits);
      vTaskDelay(pdMS_TO_TICKS(1000));
      rot = 0;
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if ((i++ % 10) == true) {
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
      // Выводим значение счетчика
      // ESP_LOGI(COUNTER_TAG, "Counter[%d]: %ld  >>  %ld", (int)isOn, rot,
      //          app_state.water_target);
    }
    vTaskDelay(xBlockTime);
  }
}
