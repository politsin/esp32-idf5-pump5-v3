#include "counter_cycle.h"

#include "counterTask.h"
#include "screenTask.h"
#include "telegram_manager.h"
#include "util/config.h"
#include "util/telemetry_manager.h"

#include "../util/pcf8575_io.h"

#include "esp_log.h"

namespace {
static const char *TAG = "COUNTER";
constexpr int kProgressReportInterval = 50;
#ifndef VALVE_SWITCH_BIT
#define VALVE_SWITCH_BIT (1UL << 29)
#endif
}

void counter_process_tick_progress(int32_t &rot,
                                   bool pump_on,
                                   bool flush_mode,
                                   int32_t &current_valve,
                                   TickType_t &valve_start_time,
                                   TickType_t valve_work_times[NUM_VALVES],
                                   TickType_t &pump_start_time,
                                   int32_t &pump_start_counter,
                                   int32_t valve_targets[NUM_VALVES],
                                   int32_t overpour_ticks[NUM_VALVES],
                                   int &pending_close_valve,
                                   int &pending_open_valve,
                                   bool &valve_switch_pending,
                                   TaskHandle_t counter_task) {
  if (!pump_on || flush_mode) return;

  const int idx0 = current_valve - 1;
  const int32_t target = valve_targets[idx0];
  if (rot < target) return;

  counter_targets_update_overpour(overpour_ticks, idx0, rot - target);
  const TickType_t current_time = xTaskGetTickCount();
  const TickType_t valve_time = current_time - valve_start_time;
  app_state.valve_times[idx0] = valve_time;
  valve_work_times[idx0] = valve_time;
  valve_start_time = current_time;
  pending_close_valve = current_valve;
  current_valve++;
  if (current_valve > NUM_VALVES) current_valve = 1;
  app_state.valve = current_valve;
  app_state.banks_count++;
  app_state.total_banks_count++;
  app_state.today_banks_count++;
  (void)save_total_banks_count(app_state.total_banks_count);
  (void)save_today_banks_count(app_state.today_banks_count);
  (void)telemetry_send_fill_event(app_state.banks_count,
                                  app_state.today_banks_count,
                                  app_state.total_banks_count,
                                  idx0 + 1);
  pending_open_valve = current_valve;
  valve_switch_pending = true;
  xTaskNotify(counter_task, VALVE_SWITCH_BIT, eSetBits);
  if (app_state.banks_count % kProgressReportInterval == 0) {
    xTaskNotify(counter_task, PROGRESS_REPORT_BIT, eSetBits);
  }

  rot = 0;
  pump_start_counter = 0;
  pump_start_time = xTaskGetTickCount();
}

bool counter_check_dry_run(bool &is_on,
                           bool &pump_on,
                           TickType_t &pump_start_time,
                           int32_t pump_start_counter,
                           int32_t rot,
                           int32_t &current_valve,
                           TickType_t &valve_start_time,
                           const CounterRuntimeSettings &runtime) {
  if (!is_on || pump_start_time <= 0) return false;

  const TickType_t pump_work_time = xTaskGetTickCount() - pump_start_time;
  const int32_t counter_increase = rot - pump_start_counter;
  if (pump_work_time <= pdMS_TO_TICKS(runtime.dry_run_timeout_ms) ||
      counter_increase >= runtime.dry_run_min_ticks) {
    return false;
  }

  ESP_LOGW(TAG, "DRY RUN PROTECTION! Pump working for %ldms but counter increased only by %ld",
           static_cast<long>(runtime.dry_run_timeout_ms), static_cast<long>(counter_increase));

  app_state.rock = false;
  is_on = false;
  pump_on = false;
  ioexp_set_all_valves(false);
  app_state.valve = 0;
  current_valve = 0;
  valve_start_time = 0;

  char message[512];
  snprintf(message, sizeof(message),
           "🚰 🚨 АВАРИЯ! Счётчик не работает!\n"
           "Помпа работала %ld мс, но счётчик увеличился только на %ld (порог %ld)\n"
           "Налито банок: %ld\n"
           "Расход в литрах: %ld\n"
           "Время работы: %02ld:%02ld\n"
           "Налито сегодня: %ld банок\n"
           "Всего налито с момента старта устройства: %ld банок",
           static_cast<long>(runtime.dry_run_timeout_ms), counter_increase,
           static_cast<long>(runtime.dry_run_min_ticks), app_state.banks_count,
           app_state.banks_count / 4, (pump_work_time / 100) / 60, ((pump_work_time / 100) % 60),
           app_state.today_banks_count, app_state.total_banks_count);
  telegram_send_message(message);

  vTaskDelay(pdMS_TO_TICKS(300));
  ioexp_set_pump(false);
  pump_start_time = 0;
  return true;
}
