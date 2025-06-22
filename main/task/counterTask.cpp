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
#include "telegram_manager.h"
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

// Массив для времени работы клапанов (обновляется в ISR)
static volatile TickType_t valve_work_times[5] = {0, 0, 0, 0, 0};

static TickType_t pump_start_time = 0; // Время старта помпы для защиты
static int32_t pump_start_counter = 0; // Значение счётчика при старте помпы

// Флаг для отправки предупреждения о сухом ходе
static bool warning_sent = false;

// Флаг режима промывки
static bool flush_mode = false;

// Переменная для отслеживания изменений клапанов
static int32_t last_valve = 0;

// Массив целей для каждого клапана (пока все одинаковые)
// 1075 - 250 ml
static int32_t valve_targets[5] = {1075, 1075, 1075, 1075, 1075};

// Переменные для корректировки цели на основе скорости
static int32_t last_correction_rot = 0;
static TickType_t last_correction_time = 0;
#define CORRECTION_INTERVAL 50
static const int32_t BASE_TARGET = 1075; // Базовая цель для 250мл
static const int32_t TARGET_ML = 250; // Целевой объём в мл

// Простая линейная экстраполяция по двум точкам
static const float NORMAL_TIME = 7.0f;    // Нормальное время (7 сек)
static const float SLOW_TIME = 15.0f;     // Медленное время (15 сек)
static const float NORMAL_ML = 250.0f;    // Нормальный объём (250мл)
static const float SLOW_ML = 272.0f;      // Объём при медленной скорости (272мл)

#define NORMAL_SPEED_ML_PER_SECOND (TARGET_ML / NORMAL_TIME) // 250/7 ≈ 35.7 мл/с

// Глобальные переменные для накопления перелитых тиков
static int32_t accumulated_overpoured_ticks[5] = {0, 0, 0, 0, 0};

// Массив предварительно рассчитанных тиков коррекции для скоростей от 30% до 100%
static int32_t speed_correction_ticks[71]; // 71 элемент: от 30% до 100%

// Функция для расчёта тиков коррекции на основе процента скорости
static int32_t calculate_correction_ticks(int speed_percent) {
    if (speed_percent >= 100) {
        return 0; // Нормальная скорость - без коррекции
    }
    
    // Получаем текущую базовую цель с учётом энкодера
    int32_t current_base_target = app_config.steps + app_state.encoder;
    
    // При скорости 50% target должен быть 988 тиков
    // За 21 корректировку: (current_base_target-988)/21 тиков за корректировку
    // При скорости 50% вычитаем 4 тика за корректировку
    int32_t persent = 88;
    int32_t target_correction = current_base_target * persent / 100;
    int32_t ticks_per_iteration = (current_base_target - target_correction) / (current_base_target / CORRECTION_INTERVAL);
    
    // Пропорционально для других скоростей (чем больше скорость, тем меньше коррекция)
    float speed_ratio = (100.0f - speed_percent) / 50.0f; // относительно 50%
    return (int32_t)(ticks_per_iteration * speed_ratio);
}

// Функция для заполнения массива коррекций при старте
static void init_speed_correction_array() {
    ESP_LOGW(COUNTER_TAG, "Initializing speed correction array:");
    for (int speed = 30; speed <= 100; speed++) {
        int32_t correction = calculate_correction_ticks(speed);
        speed_correction_ticks[speed - 30] = correction;
        // ESP_LOGW(COUNTER_TAG, "Speed %d%% -> correction %ld ticks", speed, correction);
    }
}

// Функция обработки прерывания
static void IRAM_ATTR counter_isr_handler(void *arg) {
    rot = rot + 1;
    app_state.water_current = rot;

    if (pumpOn && !flush_mode) {
        // Инициализация первого клапана при старте
        if (current_valve == 0) {
            current_valve = 1;
            valve_start_time = xTaskGetTickCount();
            // Убираем лог из ISR - вызывает краш
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
            app_state.valve_times[current_valve - 1] = valve_time; // Сохраняем в тиках для точности
            
            // Обновляем время работы клапана в массиве (в тиках)
            valve_work_times[current_valve - 1] = valve_time;
            
            valve_start_time = current_time;
            
            // Убираем отладочный лог из ISR - может вызывать проблемы
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
            
            // Сброс значений коррекции при переключении клапана
            last_correction_rot = 0;
            last_correction_time = xTaskGetTickCount();
            
            // Сброс target для нового клапана
            valve_targets[current_valve - 1] = app_config.steps + app_state.encoder;
            
            // Сброс накопленных перелитых тиков для нового клапана
            accumulated_overpoured_ticks[current_valve - 1] = 0;

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
  bool isOn = false;
  uint32_t i = 0;
  uint32_t notification;
  app_state.water_target = app_config.steps + app_state.encoder;
  
  // Инициализируем массив целей для каждого клапана
  for (int i = 0; i < 5; i++) {
    valve_targets[i] = app_config.steps + app_state.encoder;
  }
  
  // Инициализируем previous_target базовым значением из настроек
  app_state.previous_target = app_config.steps + app_state.encoder;
  
  // Инициализируем массив коррекций скоростей
  init_speed_correction_array();

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
        // Промывка возможна только из состояния idle (после остановки)
        if (isOn || app_state.start_time > 0) {
          ESP_LOGW(COUNTER_TAG, "Flush rejected: system is running. Stop first!");
          continue;
        }
        
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
        
        // Делаем 2 круга: каждый клапан на 1 секунду
        for (int round = 0; round < 2; round++) {
          for (int valve = 1; valve <= 5; valve++) {
            // Проверяем STOP каждые 100мс
            uint32_t stop_check = 0;
            if (xTaskNotifyWait(0x0, ULONG_MAX, &stop_check, pdMS_TO_TICKS(100)) == pdTRUE) {
              if (stop_check & BTN_STOP_BIT) {
                ESP_LOGW(COUNTER_TAG, "STOP during flush!");
                goto flush_stopped;
              }
            }
            
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
            ESP_LOGW(COUNTER_TAG, "Flush: valve %ld, round %d", (long)valve, round + 1);
            
            // Ждём 1 секунду с проверкой STOP каждые 100мс
            for (int i = 0; i < 10; i++) {
              if (xTaskNotifyWait(0x0, ULONG_MAX, &stop_check, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (stop_check & BTN_STOP_BIT) {
                  ESP_LOGW(COUNTER_TAG, "STOP during flush!");
                  goto flush_stopped;
                }
              }
            }
            
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
        
        // Ждём 1 секунду и выключаем помпу
        for (int i = 0; i < 10; i++) {
          uint32_t stop_check = 0;
          if (xTaskNotifyWait(0x0, ULONG_MAX, &stop_check, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (stop_check & BTN_STOP_BIT) {
              ESP_LOGW(COUNTER_TAG, "STOP during flush!");
              goto flush_stopped;
            }
          }
        }
        
        // В конце промывки ещё на 2 секунды открываем все клапаны
        gpio_set_level(VALVE1, 1);
        gpio_set_level(VALVE2, 1);
        gpio_set_level(VALVE3, 1);
        gpio_set_level(VALVE4, 1);
        gpio_set_level(VALVE5, 1);
        app_state.valve = 0; // Специальное значение для отображения всех клапанов
        xTaskNotify(screen, UPDATE_BIT, eSetBits); // Обновляем экран
        
        // Ждём 2 секунды с проверкой STOP
        for (int i = 0; i < 20; i++) {
          uint32_t stop_check = 0;
          if (xTaskNotifyWait(0x0, ULONG_MAX, &stop_check, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (stop_check & BTN_STOP_BIT) {
              ESP_LOGW(COUNTER_TAG, "STOP during flush!");
              goto flush_stopped;
            }
          }
        }
        
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
        continue; // Переходим к следующей итерации цикла
        
        flush_stopped:
        // Обработка остановки промывки
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
        flush_mode = false;
        xTaskNotify(screen, UPDATE_BIT, eSetBits);
        ESP_LOGW(COUNTER_TAG, "Flush stopped by user!");
        continue;
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
          valve_work_times[i] = 0; // Сбрасываем время работы клапанов
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
        // Инициализация переменных для коррекции скорости
        last_correction_rot = 0;
        last_correction_time = xTaskGetTickCount();
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
          
          // Отправляем уведомление о нажатии STOP с иконкой
          telegram_send_button_press_with_icon("🔴", "STOP");
          
          // Отправляем полный отчёт
          telegram_send_completion_report(app_state.banks_count, total_time);
        }
        app_state.start_time = 0; // Останавливаем время
        
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
      ESP_LOGW(COUNTER_TAG, "VALVE CHANGED: %d -> %ld", (int)last_valve, (long)app_state.valve);
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
        snprintf(
            message, sizeof(message),
            "🚰 🚨 АВАРИЯ! Счётчик не работает!\n"
            "Помпа работала 6 секунд, но счётчик увеличился только на %ld\n"
            "Налито банок: %ld\n"
            "Расход в литрах: %ld\n"
            "Время работы: %02ld:%02ld",
            counter_increase, app_state.banks_count, app_state.banks_count / 4,
            (pump_work_time / 100) / 60, ((pump_work_time / 100) % 60));
        telegram_send_message(message);
        
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(PUMP, 0);
        pump_start_time = 0;
      }
    }
    
    // Коррекция цели каждые 50 тиков на основе общего времени работы клапана
    if (rot - last_correction_rot >= CORRECTION_INTERVAL) {
      TickType_t now = xTaskGetTickCount();
      
      // Считаем общее время работы текущего клапана
      float total_valve_time = (now - valve_start_time) / 100.0f;
      
      if (total_valve_time > 0.1f) { // чтобы не было деления на ноль
        // Рассчитываем текущую скорость налива
        int32_t current_base_target = app_config.steps + app_state.encoder;
        float current_speed_ml_per_second = (rot * TARGET_ML) / (current_base_target * total_valve_time);
        
        // Коррекция цели на основе скорости потока
        if (current_speed_ml_per_second < NORMAL_SPEED_ML_PER_SECOND) {
            // Получаем процент скорости
            int speed_percent = (int)((current_speed_ml_per_second / NORMAL_SPEED_ML_PER_SECOND) * 100.0f);
            
            // Получаем тики коррекции из предварительно рассчитанного массива
            int32_t ticks_per_iteration = 0;
            if (speed_percent >= 30 && speed_percent <= 100) {
                ticks_per_iteration = speed_correction_ticks[speed_percent - 30];
            }
            
            // Уменьшаем базовый target на фиксированное количество тиков
            int32_t new_target = valve_targets[current_valve - 1] - ticks_per_iteration;
            
            // Каждый раз устанавливаем цель на рассчитанное значение
            valve_targets[current_valve - 1] = new_target;
            
            // Обновляем previous_target для отображения на экране (базовый target из настроек)
            app_state.previous_target = app_config.steps + app_state.encoder;
            app_state.water_target = new_target;
            
            // ESP_LOGW(COUNTER_TAG, "Speed: %d%% -> correction %ld ticks
            //  -> target %ld", speed_percent, ticks_per_iteration, new_target);
        }
        last_correction_rot = rot;
      }
    }
    
    if ((i++ % 20) == true) {
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }
    vTaskDelay(xBlockTime);
  }
}
