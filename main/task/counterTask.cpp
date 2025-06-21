// #include <counterTask.h> WTF o.O
// Counter #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype DI = GPIO_NUM_13;
static constexpr Pintype PUMP = GPIO_NUM_25;
static constexpr Pintype VALVE5 = GPIO_NUM_17;
static constexpr Pintype VALVE4 = GPIO_NUM_22;
static constexpr Pintype VALVE3 = GPIO_NUM_21;
static constexpr Pintype VALVE2 = GPIO_NUM_26;
static constexpr Pintype VALVE1 = GPIO_NUM_27;
#include "sdkconfig.h"
#include <config.h>
#include <esp_log.h>
#include <main.h>
#include <rom/gpio.h>
#define COUNTER_TAG "COUNTER"

#include "task/screenTask.h"
#include "../util/telegram_manager.h"
TaskHandle_t counter;

// Объявляем счетчик как глобальную переменную
static volatile int32_t rot = 0;

static volatile bool pumpOn = false;
static volatile bool valve1On = false;
static volatile bool valve2On = false;
static volatile bool valve3On = false;
static volatile bool valve4On = false;
static volatile bool valve5On = false;

// Переменные для отслеживания времени клапанов
static volatile TickType_t valve_start_time = 0;
static int32_t current_valve = 0;

static TickType_t pump_start_time = 0; // Время старта помпы для защиты
static int32_t pump_start_counter = 0; // Значение счётчика при старте помпы

// Функция обработки прерывания
static void IRAM_ATTR counter_isr_handler(void *arg) {
  // Увеличиваем счетчик при каждом прерывании
  rot = rot + 1;
  app_state.water_current = rot;
  if (pumpOn) {
    int32_t target = app_state.water_target;
    if (rot < target) {
      if (current_valve != 1) {
        // Завершаем предыдущий клапан
        if (current_valve > 0 && current_valve <= 5) {
          TickType_t end_time = xTaskGetTickCount();
          app_state.valve_times[current_valve - 1] = (end_time - valve_start_time); // Сохраняем в тиках FreeRTOS
        }
        // Начинаем новый клапан
        current_valve = 1;
        valve_start_time = xTaskGetTickCount();
        gpio_set_level(VALVE1, 1);
        app_state.valve = 1;
        app_state.banks_count++; // Увеличиваем счётчик банок
      }
    } else if (rot > target && rot < target * 2) {
      if (current_valve != 2) {
        // Завершаем предыдущий клапан
        if (current_valve > 0 && current_valve <= 5) {
          TickType_t end_time = xTaskGetTickCount();
          app_state.valve_times[current_valve - 1] = (end_time - valve_start_time);
        }
        // Начинаем новый клапан
        current_valve = 2;
        valve_start_time = xTaskGetTickCount();
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 1);
        app_state.valve = 2;
        app_state.banks_count++; // Увеличиваем счётчик банок
      }
    } else if (rot > target * 2 && rot < target * 3) {
      if (current_valve != 3) {
        // Завершаем предыдущий клапан
        if (current_valve > 0 && current_valve <= 5) {
          TickType_t end_time = xTaskGetTickCount();
          app_state.valve_times[current_valve - 1] = (end_time - valve_start_time);
        }
        // Начинаем новый клапан
        current_valve = 3;
        valve_start_time = xTaskGetTickCount();
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 1);
        app_state.valve = 3;
        app_state.banks_count++; // Увеличиваем счётчик банок
      }
    } else if (rot > target * 3 && rot < target * 4) {
      if (current_valve != 4) {
        // Завершаем предыдущий клапан
        if (current_valve > 0 && current_valve <= 5) {
          TickType_t end_time = xTaskGetTickCount();
          app_state.valve_times[current_valve - 1] = (end_time - valve_start_time);
        }
        // Начинаем новый клапан
        current_valve = 4;
        valve_start_time = xTaskGetTickCount();
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 1);
        app_state.valve = 4;
        app_state.banks_count++; // Увеличиваем счётчик банок
      }
    } else if (rot > target * 3 && rot < target * 5) {
      if (current_valve != 5) {
        // Завершаем предыдущий клапан
        if (current_valve > 0 && current_valve <= 5) {
          TickType_t end_time = xTaskGetTickCount();
          app_state.valve_times[current_valve - 1] = (end_time - valve_start_time);
        }
        // Начинаем новый клапан
        current_valve = 5;
        valve_start_time = xTaskGetTickCount();
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 1);
        app_state.valve = 5;
        app_state.banks_count++; // Увеличиваем счётчик банок
      }
    } else if (rot > target * 5) {
      // Завершаем последний клапан
      if (current_valve > 0 && current_valve <= 5) {
        TickType_t end_time = xTaskGetTickCount();
        app_state.valve_times[current_valve - 1] = (end_time - valve_start_time);
      }
      
      if (app_state.rock) {
        rot = 0;
        current_valve = 0;
        gpio_set_level(VALVE5, 0);
        gpio_set_level(VALVE1, 1);
        pump_start_counter = 0; // Сбрасываем счётчик при переключении на второй круг
      } else {
        gpio_set_level(VALVE5, 0);
        gpio_set_level(PUMP, 0);
        app_state.valve = 0;
        current_valve = 0;
        pumpOn = false;
      }
    }
  }
}

app_config_t app_config = {
    .steps = 1075,
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
        isOn = true;
        app_state.is_on = isOn;
        app_state.valve = 0; // Специальное значение для отображения всех клапанов
        ESP_LOGW(COUNTER_TAG, "Flush!");
      }
      if (notification & BTN_RUN_BIT) {
        if (!app_state.rock) {
          // Только если не в режиме rock - сбрасываем счётчики
          app_state.rock = true;
          ESP_LOGW(COUNTER_TAG, "Run!");
          rot = 0;
          isOn = true;
          pumpOn = true;
          gpio_set_level(PUMP, isOn);
          app_state.water_delta = 0;
          // Сброс времени клапанов и счётчика банок
          for (int i = 0; i < 5; i++) {
            app_state.valve_times[i] = 0;
          }
          app_state.banks_count = 0; // Сброс счётчика банок
          app_state.start_time = xTaskGetTickCount(); // Запоминаем время старта
          pump_start_time = xTaskGetTickCount(); // Запоминаем время старта помпы для защиты
          pump_start_counter = rot; // Запоминаем значение счётчика при старте помпы
          app_state.counter_error = false; // Сбрасываем флаг ошибки счётчика
          current_valve = 0;
          valve_start_time = 0;
          xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
          vTaskDelay(pdMS_TO_TICKS(300));
          gpio_set_level(VALVE1, isOn);
          app_state.valve = 1;
        } else {
          // Если уже в режиме rock - просто продолжаем работу
          ESP_LOGW(COUNTER_TAG, "Already running, continuing...");
        }
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
        // Сброс времени клапанов
        for (int i = 0; i < 5; i++) {
          app_state.valve_times[i] = 0;
        }
        current_valve = 0;
        valve_start_time = 0;
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(VALVE1, isOn);
        app_state.valve = 1;
      }
      if (notification & BTN_STOP_BIT) {
        ESP_LOGW(COUNTER_TAG, "STOP Emergency!");
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
        current_valve = 0;
        valve_start_time = 0;
        
        // Останавливаем время и отправляем отчёт в Telegram
        if (app_state.start_time > 0) {
          int32_t total_time = xTaskGetTickCount() - app_state.start_time;
          app_state.final_time = total_time / 100; // Сохраняем финальное время в секундах
          app_state.final_banks = app_state.banks_count; // Сохраняем финальное количество банок
          app_state.start_time = 0; // Останавливаем время
          telegram_send_completion_report(app_state.banks_count, total_time);
        }
        
        pump_start_time = 0; // Сбрасываем время старта помпы
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
        
        // Отправляем уведомление в Telegram об изменении уставки
        char message[256];
        snprintf(message, sizeof(message), 
                "Изменена уставка наливайки:\n"
                "Базовая уставка: %lu\n"
                "Сдвиг энкодера: %ld\n"
                "Новая уставка: %lu", 
                app_config.steps, app_state.encoder, 
                app_state.water_target);
        telegram_send_message(message);
      }
    }
    app_state.is_on = isOn;
    app_state.water_current = rot;
    
    // Проверка защиты от сухого хода помпы
    if (isOn && pump_start_time > 0) {
      TickType_t pump_work_time = xTaskGetTickCount() - pump_start_time;
      int32_t counter_increase = rot - pump_start_counter;
      
      // Если помпа работает больше 4 секунд и счётчик увеличился меньше чем на 100
      if (pump_work_time > pdMS_TO_TICKS(4000) && counter_increase < 100) {
        ESP_LOGW(COUNTER_TAG, "DRY RUN PROTECTION! Pump working for 4s but counter increased only by %ld", counter_increase);
        
        // Автоматически останавливаем работу
        app_state.rock = false;
        isOn = false;
        pumpOn = false;
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 0);
        app_state.valve = 0;
        current_valve = 0;
        valve_start_time = 0;
        
        // Сохраняем финальные значения и отправляем отчёт
        if (app_state.start_time > 0) {
          int32_t total_time = xTaskGetTickCount() - app_state.start_time;
          app_state.final_time = total_time / 100;
          app_state.final_banks = app_state.banks_count;
          app_state.start_time = 0;
          app_state.counter_error = true; // Устанавливаем флаг ошибки счётчика
          
          // Отправляем специальное сообщение о срабатывании защиты
          char message[256];
          snprintf(message, sizeof(message), 
                  "🚨 АВАРИЯ! Счётчик не работает!\n"
                  "Помпа работала 4 секунды, но счётчик увеличился только на %ld\n"
                  "Налито банок: %ld\n"
                  "Время работы: %02ld:%02ld",
                  counter_increase, app_state.banks_count,
                  (total_time / 100) / 60, ((total_time / 100) % 60));
          telegram_send_message(message);
        }
        
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(PUMP, 0);
        pump_start_time = 0; // Сбрасываем время старта помпы
      }
    }
    
    if (!app_state.rock && rot > app_state.water_target * 5) {
      isOn = false;
      gpio_set_level(PUMP, isOn);

      // Stop timing and calculate elapsed time
      TickType_t endTime = xTaskGetTickCount();
      app_state.time = (endTime - startTime); // Convert ticks to seconds
      ESP_LOGW(COUNTER_TAG, "STOP %ld | done = %ld| Time: %ld seconds",
               app_state.water_target, rot, app_state.time);

      isOn = false;
      app_state.is_on = isOn;
      app_state.water_delta = rot - app_state.water_target * 5;
      
      // Отправляем отчёт в Telegram о завершении работы
      if (app_state.start_time > 0) {
        int32_t total_time = xTaskGetTickCount() - app_state.start_time;
        app_state.final_time = total_time / 100; // Сохраняем финальное время в секундах
        app_state.final_banks = app_state.banks_count; // Сохраняем финальное количество банок
        app_state.start_time = 0; // Останавливаем время
        telegram_send_completion_report(app_state.banks_count, total_time);
      }
      
      pump_start_time = 0; // Сбрасываем время старта помпы
      
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
