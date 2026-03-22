#pragma once

#include <stdint.h>

#include "main.h"

struct CounterRuntimeSettings {
  int32_t flush_valve_ms;
  int32_t flush_all_ms;
  int32_t dry_run_timeout_ms;
  int32_t dry_run_min_ticks;
};

void counter_targets_reload_runtime_settings(app_config_t &config,
                                             app_state_t &state,
                                             int32_t valve_targets[NUM_VALVES],
                                             int32_t current_valve,
                                             int32_t overpour_ticks[NUM_VALVES],
                                             CounterRuntimeSettings &runtime);

void counter_targets_refresh_one(const app_config_t &config,
                                 app_state_t &state,
                                 const int32_t overpour_ticks[NUM_VALVES],
                                 int32_t valve_idx0,
                                 int32_t valve_targets[NUM_VALVES]);

void counter_targets_refresh_all(const app_config_t &config,
                                 app_state_t &state,
                                 const int32_t overpour_ticks[NUM_VALVES],
                                 int32_t current_valve,
                                 int32_t valve_targets[NUM_VALVES]);

int32_t counter_targets_nominal(const app_config_t &config,
                                const app_state_t &state,
                                int32_t valve_idx0);

void counter_targets_update_overpour(int32_t overpour_ticks[NUM_VALVES],
                                     int32_t valve_idx0,
                                     int32_t overshoot_ticks);
