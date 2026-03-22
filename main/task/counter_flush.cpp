#include "counter_flush.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main.h"
#include "screenTask.h"

#include "../util/pcf8575_io.h"

#include "esp_log.h"

namespace {
static const char *TAG = "COUNTER";

bool wait_for_stop_or_timeout(int32_t wait_ms) {
  const int loops = (wait_ms + 99) / 100;
  for (int i = 0; i < loops; i++) {
    uint32_t stop_check = 0;
    if (xTaskNotifyWait(0x0, ULONG_MAX, &stop_check, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (stop_check & BTN_STOP_BIT) {
        ESP_LOGW(TAG, "STOP during flush!");
        return true;
      }
    }
  }
  return false;
}

void set_single_valve(int valve, bool state) {
  switch (valve) {
    case 1: ioexp_set_valve(1, state); break;
    case 2: ioexp_set_valve(2, state); break;
    case 3: ioexp_set_valve(3, state); break;
    case 4: ioexp_set_valve(4, state); break;
    default: break;
  }
}

void finish_flush(bool &is_on, bool &pump_on, bool &flush_mode) {
  ioexp_set_all_valves(false);
  ioexp_set_pump(false);
  is_on = false;
  pump_on = false;
  app_state.is_on = is_on;
  app_state.valve = 0;
  flush_mode = false;
  xTaskNotify(screen, UPDATE_BIT, eSetBits);
}
} // namespace

bool counter_run_flush_sequence(bool &is_on,
                                bool &pump_on,
                                bool &flush_mode,
                                int32_t flush_valve_ms,
                                int32_t flush_all_ms) {
  if (is_on || app_state.start_time > 0) {
    ESP_LOGW(TAG, "Flush rejected: system is running. Stop first!");
    return true;
  }

  ESP_LOGW(TAG, "Flush started!");
  flush_mode = true;
  ioexp_set_pump(true);
  is_on = true;
  pump_on = true;
  app_state.is_on = is_on;
  app_state.valve = 0;

  ioexp_set_all_valves(true);
  xTaskNotify(screen, UPDATE_BIT, eSetBits);

  for (int round = 0; round < 2; round++) {
    for (int valve = 1; valve <= NUM_VALVES; valve++) {
      if (wait_for_stop_or_timeout(100)) {
        finish_flush(is_on, pump_on, flush_mode);
        ESP_LOGW(TAG, "Flush stopped by user!");
        return true;
      }

      set_single_valve(valve, true);
      app_state.valve = valve;
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
      ESP_LOGW(TAG, "Flush: valve %ld, round %d", static_cast<long>(valve), round + 1);

      if (wait_for_stop_or_timeout(flush_valve_ms)) {
        finish_flush(is_on, pump_on, flush_mode);
        ESP_LOGW(TAG, "Flush stopped by user!");
        return true;
      }

      set_single_valve(valve, false);
      app_state.valve = 0;
      xTaskNotify(screen, UPDATE_BIT, eSetBits);
    }
  }

  if (wait_for_stop_or_timeout(flush_valve_ms)) {
    finish_flush(is_on, pump_on, flush_mode);
    ESP_LOGW(TAG, "Flush stopped by user!");
    return true;
  }

  ioexp_set_all_valves(true);
  app_state.valve = 0;
  xTaskNotify(screen, UPDATE_BIT, eSetBits);

  if (wait_for_stop_or_timeout(flush_all_ms)) {
    finish_flush(is_on, pump_on, flush_mode);
    ESP_LOGW(TAG, "Flush stopped by user!");
    return true;
  }

  finish_flush(is_on, pump_on, flush_mode);
  ESP_LOGW(TAG, "Flush completed!");
  xTaskNotify(screen, BTN_STOP_BIT, eSetBits);
  return true;
}
