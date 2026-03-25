// #include <counterTask.h> WTF o.O
// Counter #22 #19.
#include "counterTask.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <config.h>
#include <esp_log.h>
#include <main.h>

#include "../util/pcf8575_io.h"
#include "counter_cycle.h"
#include "counter_flush.h"
#include "counter_targets.h"
#include "counter_ticks.h"
#include "task/screenTask.h"
#include "telegram_manager.h"

#define COUNTER_TAG "COUNTER"

TaskHandle_t counter;

static int32_t rot = 0;
static bool pumpOn = false;
static TickType_t valve_start_time = 0;
static TickType_t valve_work_times[NUM_VALVES] = {0, 0, 0, 0};
static TickType_t pump_start_time = 0;
static int32_t pump_start_counter = 0;
static bool flush_mode = false;
static int32_t current_valve = 0;
static int32_t last_valve = 0;
static int32_t valve_targets[NUM_VALVES] = {
    APP_DEFAULT_TARGET_TICKS,
    APP_DEFAULT_TARGET_TICKS,
    APP_DEFAULT_TARGET_TICKS,
    APP_DEFAULT_TARGET_TICKS,
};
static int32_t accumulated_overpoured_ticks[NUM_VALVES] = {0, 0, 0, 0};
static bool encoder_change_pending = false;
static TickType_t encoder_change_last_time = 0;
static const TickType_t ENCODER_REPORT_SILENCE = pdMS_TO_TICKS(700);

static int pending_close_valve = 0;
static int pending_open_valve = 0;
static bool valve_switch_pending = false;

static CounterRuntimeSettings g_runtime = {
    .flush_valve_ms = 1000,
    .flush_all_ms = 2000,
    .dry_run_timeout_ms = 3000,
    .dry_run_min_ticks = 50,
};

#ifndef VALVE_SWITCH_BIT
#define VALVE_SWITCH_BIT (1UL << 29)
#endif

void counter_reload_runtime_settings() {
  counter_targets_reload_runtime_settings(app_config, app_state, valve_targets, current_valve,
                                          accumulated_overpoured_ticks, g_runtime);
}

app_config_t app_config = {
    .steps = APP_DEFAULT_TARGET_TICKS,
    .encoder = 0,
    .valve_offset = {0, 0, 0, 0},
};

void counterTask(void *pvParam) {
  // Помпа через PCF8575
  ioexp_set_pump(false);
  // Все клапаны выключены через PCF8575
  ioexp_set_all_valves(false);
  CounterTickSource tick_source = {};
  counter_ticks_init(tick_source);
  const TickType_t xBlockTime = pdMS_TO_TICKS(50);
  counter_reload_runtime_settings();
  ESP_LOGW(COUNTER_TAG, "Settings: steps=%lu encoder=%ld flush_valve_ms=%ld flush_all_ms=%ld",
           (unsigned long)app_config.steps, (long)app_state.encoder,
           (long)g_runtime.flush_valve_ms, (long)g_runtime.flush_all_ms);
  ESP_LOGW(COUNTER_TAG, "Dry-run protection: timeout_ms=%ld min_ticks=%ld",
           (long)g_runtime.dry_run_timeout_ms, (long)g_runtime.dry_run_min_ticks);

  bool isOn = false;
  uint32_t i = 0;
  uint32_t notification;
  counter_targets_refresh_all(app_config, app_state, accumulated_overpoured_ticks,
                              current_valve, valve_targets);
  ESP_LOGW(COUNTER_TAG,
           "Initial target: base=%ld encoder=%ld target_p1=%ld offsets=[%ld,%ld,%ld,%ld]",
           (long)app_config.steps,
           (long)app_state.encoder,
           (long)valve_targets[0],
           (long)app_config.valve_offset[0],
           (long)app_config.valve_offset[1],
           (long)app_config.valve_offset[2],
           (long)app_config.valve_offset[3]);
  
  while (true) {
    int32_t pending_ticks = counter_ticks_take_pending_gpio();
    if (pending_ticks > 0) {
      rot += pending_ticks;
      app_state.water_current = rot;
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }

    int32_t pcnt_ticks = 0;
    if (counter_ticks_try_take_pcnt(tick_source, pcnt_ticks)) {
      rot += pcnt_ticks;
      app_state.water_current = rot;
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
      counter_process_tick_progress(rot, pumpOn, flush_mode, current_valve, valve_start_time,
                                    valve_work_times, pump_start_time, pump_start_counter,
                                    valve_targets, accumulated_overpoured_ticks,
                                    pending_close_valve, pending_open_valve,
                                    valve_switch_pending, counter);
      counter_targets_refresh_one(app_config, app_state, accumulated_overpoured_ticks,
                                  current_valve - 1, valve_targets);
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    } else if (!tick_source.pcnt_ready && rot > 0) {
      app_state.water_current = rot;
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
      counter_process_tick_progress(rot, pumpOn, flush_mode, current_valve, valve_start_time,
                                    valve_work_times, pump_start_time, pump_start_counter,
                                    valve_targets, accumulated_overpoured_ticks,
                                    pending_close_valve, pending_open_valve,
                                    valve_switch_pending, counter);
      counter_targets_refresh_one(app_config, app_state, accumulated_overpoured_ticks,
                                  current_valve - 1, valve_targets);
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }
    if (xTaskNotifyWait(0x0, ULONG_MAX, &notification, 0) ==
        pdTRUE) { // Wait for any notification
      if (notification & RESET_TICKS_BIT) {
        rot = 0;
        app_state.water_current = 0;
        app_state.water_delta = 0;
        pump_start_counter = 0;
        counter_ticks_reset(tick_source);
        xTaskNotify(screen, UPDATE_BIT, eSetBits);
      }
      if (notification & VALVE_SWITCH_BIT) {
        // Выполнить переключение клапанов в контексте задачи (разрешён I2C)
        int close_v = pending_close_valve;
        int open_v  = pending_open_valve;
        pending_close_valve = 0;
        valve_switch_pending = false;
        if (close_v >= 1 && close_v <= NUM_VALVES) {
          ioexp_set_valve(close_v, false);
        }
        if (open_v >= 1 && open_v <= NUM_VALVES) {
          ioexp_set_valve(open_v, true);
        }
      }
      if (notification & BTN_FLUSH_BIT) {
        counter_run_flush_sequence(isOn, pumpOn, flush_mode,
                                   g_runtime.flush_valve_ms, g_runtime.flush_all_ms);
        continue;
      }
      if (notification & BTN_RUN_BIT) {
        // Каждый старт всегда начинается с первого клапана
        app_state.rock = true;
        ESP_LOGW(COUNTER_TAG, "Run!");
        rot = 0;
        // Важно: цель могла быть изменена через web/NVS после старта задачи.
        // Обновляем targets при каждом RUN из текущих app_config/app_state.
        counter_targets_refresh_all(app_config, app_state, accumulated_overpoured_ticks,
                                    current_valve, valve_targets);
        isOn = true;
        pumpOn = true;
        ioexp_set_pump(isOn);
        app_state.water_delta = 0;
        // Сброс времени клапанов и счётчика банок
        for (int i = 0; i < NUM_VALVES; i++) {
          app_state.valve_times[i] = 0;
          valve_work_times[i] = 0; // Сбрасываем время работы клапанов
        }
        app_state.banks_count = 0; // Сброс счётчика банок
        app_state.start_time = xTaskGetTickCount(); // Запоминаем время старта
        pump_start_time = xTaskGetTickCount(); // Запоминаем время старта помпы для защиты
        pump_start_counter = rot; // Запоминаем значение счётчика при старте помпы
        app_state.counter_error = false; // Сбрасываем флаг ошибки счётчика
        current_valve = 1; // Устанавливаем 1, чтобы сразу начать с первого клапана
        valve_start_time = xTaskGetTickCount(); // Начинаем отсчёт времени для первого клапана
        // Выключаем режим промывки при старте обычной работы
        flush_mode = false;
        // СРАЗУ открываем первый клапан при старте
        app_state.valve = 1;
        // Для UI: показываем уставку первого клапана (с учётом base+encoder+offset)
        counter_targets_refresh_one(app_config, app_state, accumulated_overpoured_ticks,
                                    0, valve_targets);
        ioexp_set_valve(1, true);
        ioexp_set_valve(2, false);
        ioexp_set_valve(3, false);
        ioexp_set_valve(4, false);
        
        xTaskNotify(screen, COUNTER_START_BIT, eSetBits);
        vTaskDelay(pdMS_TO_TICKS(300));
      }
      if (notification & BTN_STOP_BIT) {
        ESP_LOGW(COUNTER_TAG, "STOP Emergency!");
        app_state.rock = false;
        rot = 0;
        isOn = false;
        pumpOn = false;
        ioexp_set_all_valves(false);
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
        ioexp_set_pump(false);
      }
      if (notification & ENCODER_CHANGED_BIT) {
        app_config.encoder = app_state.encoder;
        counter_targets_refresh_all(app_config, app_state, accumulated_overpoured_ticks,
                                    current_valve, valve_targets);
        
        xTaskNotify(screen, UPDATE_BIT, eSetBits);
        ESP_LOGW(COUNTER_TAG, "Encoder changed: %ld", app_state.encoder);
        
        // Не шлём сразу — только отметим и подождём «тишину», чтобы отправить один итог
        encoder_change_pending = true;
        encoder_change_last_time = xTaskGetTickCount();
      }
      if (notification & PROGRESS_REPORT_BIT) {
        // Отправляем промежуточный отчёт о прогрессе
        if (app_state.start_time > 0) {
          int32_t current_time = xTaskGetTickCount() - app_state.start_time;
          telegram_send_progress_report(app_state.banks_count, current_time);
        }
      }
    }
    
    // Если были изменения уставки, а новых не было ENCODER_CHANGED_BIT в течение ENCODER_REPORT_SILENCE — шлём одно итоговое сообщение
    if (encoder_change_pending) {
      TickType_t now = xTaskGetTickCount();
      if (now - encoder_change_last_time >= ENCODER_REPORT_SILENCE) {
        encoder_change_pending = false;
        const esp_err_t save_err = config_save_pump_settings(
            (int32_t)app_config.steps,
            app_config.encoder,
            g_runtime.flush_valve_ms,
            g_runtime.flush_all_ms);
        if (save_err != ESP_OK) {
          ESP_LOGE(COUNTER_TAG, "Failed to save encoder to NVS: %s", esp_err_to_name(save_err));
        }
        char message[256];
        snprintf(message, sizeof(message),
                 "Изменена уставка наливайки:\n"
                 "Базовая уставка: %lu\n"
                 "Сдвиг энкодера: %ld\n"
                 "Новая уставка: %lu",
                 app_config.steps, app_state.encoder,
                 app_config.steps + app_state.encoder);
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
    
    counter_check_dry_run(isOn, pumpOn, pump_start_time, pump_start_counter, rot,
                          current_valve, valve_start_time, g_runtime);
    
    if ((i++ % 20) == true) {
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }
    vTaskDelay(xBlockTime);
  }
}
