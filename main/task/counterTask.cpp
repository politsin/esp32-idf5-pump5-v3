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

// Флаг для отправки предупреждения о сухом ходе
static bool warning_sent = false;

// Флаг режима промывки
static bool flush_mode = false;

// Переменная для отслеживания изменений клапанов
static int32_t last_valve = 0;

// Массив целей для каждого клапана (пока все одинаковые)
static int32_t valve_targets[5] = {1075, 1075, 1075, 1075, 1075};

// Функция обработки прерывания
static void IRAM_ATTR counter_isr_handler(void *arg) {
    rot = rot + 1;
    app_state.water_current = rot;

    if (pumpOn && !flush_mode) {
        // Инициализация первого клапана при старте
        if (current_valve == 0) {
            current_valve = 1;
            valve_start_time = xTaskGetTickCount();
            app_state.valve = 1;
            gpio_set_level(VALVE1, 1);
            gpio_set_level(VALVE2, 0);
            gpio_set_level(VALVE3, 0);
            gpio_set_level(VALVE4, 0);
            gpio_set_level(VALVE5, 0);
            return;
        }

        int32_t target = valve_targets[current_valve - 1];

        if (rot >= target) {
            // Сохраняем время работы текущего клапана
            TickType_t current_time = xTaskGetTickCount();
            TickType_t valve_time = current_time - valve_start_time;
            app_state.valve_times[current_valve - 1] = valve_time / 100;
            valve_start_time = current_time;

            // Закрываем текущий клапан
            switch (current_valve) {
                case 1: gpio_set_level(VALVE1, 0); break;
                case 2: gpio_set_level(VALVE2, 0); break;
                case 3: gpio_set_level(VALVE3, 0); break;
                case 4: gpio_set_level(VALVE4, 0); break;
                case 5: gpio_set_level(VALVE5, 0); break;
            }

            // Следующий клапан по кругу
            current_valve++;
            if (current_valve > 5) current_valve = 1;
            app_state.valve = current_valve;
            app_state.banks_count++;

            // Открываем новый клапан
            switch (current_valve) {
                case 1: gpio_set_level(VALVE1, 1); break;
                case 2: gpio_set_level(VALVE2, 1); break;
                case 3: gpio_set_level(VALVE3, 1); break;
                case 4: gpio_set_level(VALVE4, 1); break;
                case 5: gpio_set_level(VALVE5, 1); break;
            }

            // Сброс счётчика и времени старта помпы для защиты от сухого хода
            rot = 0;
            pump_start_counter = 0;
            pump_start_time = xTaskGetTickCount(); // Сбрасываем время старта помпы

            xTaskNotifyFromISR(screen, UPDATE_BIT, eSetBits, NULL);
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
  
  // Инициализируем массив целей для каждого клапана
  for (int i = 0; i < 5; i++) {
    valve_targets[i] = app_config.steps;
  }

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
        ESP_LOGW(COUNTER_TAG, "Flush started!");
        
        // Включаем режим промывки
        flush_mode = true;
        
        // Включаем помпу
        gpio_set_level(PUMP, 1);
        isOn = true;
        pumpOn = true;
        app_state.is_on = isOn;
        app_state.valve = 0; // Специальное значение для отображения всех клапанов
        
        // СРАЗУ открываем все клапаны в самом начале
        gpio_set_level(VALVE1, 1);
        gpio_set_level(VALVE2, 1);
        gpio_set_level(VALVE3, 1);
        gpio_set_level(VALVE4, 1);
        gpio_set_level(VALVE5, 1);
        app_state.valve = 0; // Специальное значение для отображения всех клапанов
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Ждём 3 секунды
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Закрываем все клапаны
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 0);
        app_state.valve = 0; // Все клапаны закрыты
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Делаем 2 круга: каждый клапан на 1 секунду
        for (int round = 0; round < 2; round++) {
          for (int valve = 1; valve <= 5; valve++) {
            // Открываем нужный клапан
            switch (valve) {
              case 1: gpio_set_level(VALVE1, 1); break;
              case 2: gpio_set_level(VALVE2, 1); break;
              case 3: gpio_set_level(VALVE3, 1); break;
              case 4: gpio_set_level(VALVE4, 1); break;
              case 5: gpio_set_level(VALVE5, 1); break;
            }
            
            app_state.valve = valve;
            xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
            ESP_LOGW(COUNTER_TAG, "Flush: valve %d, round %d", valve, round + 1);
            
            // Ждём 1 секунду
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Закрываем клапан
            switch (valve) {
              case 1: gpio_set_level(VALVE1, 0); break;
              case 2: gpio_set_level(VALVE2, 0); break;
              case 3: gpio_set_level(VALVE3, 0); break;
              case 4: gpio_set_level(VALVE4, 0); break;
              case 5: gpio_set_level(VALVE5, 0); break;
            }
            
            app_state.valve = 0; // Клапан закрыт
            xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
          }
        }
        
        // Закрываем все клапаны
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 0);
        app_state.valve = 0; // Все клапаны закрыты
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Ждём 1 секунду и выключаем помпу
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // В конце промывки ещё на 2 секунды открываем все клапаны
        gpio_set_level(VALVE1, 1);
        gpio_set_level(VALVE2, 1);
        gpio_set_level(VALVE3, 1);
        gpio_set_level(VALVE4, 1);
        gpio_set_level(VALVE5, 1);
        app_state.valve = 0; // Специальное значение для отображения всех клапанов
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Ждём 2 секунды
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Закрываем все клапаны и выключаем помпу
        gpio_set_level(VALVE1, 0);
        gpio_set_level(VALVE2, 0);
        gpio_set_level(VALVE3, 0);
        gpio_set_level(VALVE4, 0);
        gpio_set_level(VALVE5, 0);
        gpio_set_level(PUMP, 0);
        isOn = false;
        pumpOn = false;
        app_state.is_on = isOn;
        app_state.valve = 0;
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Выключаем режим промывки
        flush_mode = false;
        
        ESP_LOGW(COUNTER_TAG, "Flush completed!");
      }
      if (notification & BTN_RUN_BIT) {
        // Каждый старт всегда начинается с первого клапана
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
        current_valve = 0; // Устанавливаем 0, чтобы первый переключатель открыл клапан 1
        valve_start_time = xTaskGetTickCount(); // Начинаем отсчёт времени для первого клапана
        // Сбрасываем флаг предупреждения о сухом ходе
        warning_sent = false;
        // Выключаем режим промывки при старте обычной работы
        flush_mode = false;
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
        vTaskDelay(pdMS_TO_TICKS(300));
        // НЕ управляем клапанами здесь - только в прерывании!
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
        
        // Выключаем режим промывки при остановке
        flush_mode = false;
        
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
        
        // Обновляем массив целей для всех клапанов
        for (int i = 0; i < 5; i++) {
          valve_targets[i] = app_config.steps + app_state.encoder;
        }
        
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
    
    // Отслеживаем изменения клапанов для отладки
    if (app_state.valve != last_valve) {
      ESP_LOGW(COUNTER_TAG, "VALVE CHANGED: %d -> %d", (int)last_valve, app_state.valve);
      last_valve = app_state.valve;
    }
    
    // Проверка защиты от сухого хода помпы
    if (isOn && pump_start_time > 0) {
      TickType_t pump_work_time = xTaskGetTickCount() - pump_start_time;
      int32_t counter_increase = rot - pump_start_counter;
      
      // Если помпа работает больше 6 секунд и счётчик увеличился меньше чем на 50
      if (pump_work_time > pdMS_TO_TICKS(6000) && counter_increase < 50) {
        ESP_LOGW(COUNTER_TAG, "DRY RUN PROTECTION! Pump working for 6s but counter increased only by %ld", counter_increase);
        
        // АВТОМАТИЧЕСКИ ОСТАНАВЛИВАЕМ РАБОТУ
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
        
        // Отправляем аварийное сообщение
        char message[512];
        snprintf(message, sizeof(message), 
                "🚰 🚨 АВАРИЯ! Счётчик не работает!\n"
                "Помпа работала 6 секунд, но счётчик увеличился только на %ld\n"
                "Налито банок: %ld\n"
                "Время работы: %02ld:%02ld",
                counter_increase, app_state.banks_count,
                (pump_work_time / 100) / 60, ((pump_work_time / 100) % 60));
        telegram_send_message(message);
        
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(PUMP, 0);
        pump_start_time = 0;
      }
    }
    
    if ((i++ % 20) == true) {
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }
    vTaskDelay(xBlockTime);
  }
}
