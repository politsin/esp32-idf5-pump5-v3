#include "counter_targets.h"

#include "config.h"

namespace {
constexpr int32_t kMaxOverpourCompTicks = 200;

int32_t clamp_valve_idx(int32_t valve_idx0) {
  if (valve_idx0 < 0) return 0;
  if (valve_idx0 >= NUM_VALVES) return NUM_VALVES - 1;
  return valve_idx0;
}

int32_t current_base_target_ticks(const app_config_t &config, const app_state_t &state) {
  return static_cast<int32_t>(config.steps) + state.encoder;
}

int32_t effective_target_ticks(const app_config_t &config,
                               const app_state_t &state,
                               const int32_t overpour_ticks[NUM_VALVES],
                               int32_t valve_idx0) {
  valve_idx0 = clamp_valve_idx(valve_idx0);
  const int32_t nominal = counter_targets_nominal(config, state, valve_idx0);

  int32_t compensation = overpour_ticks[valve_idx0];
  if (compensation < 0) compensation = 0;
  if (compensation > kMaxOverpourCompTicks) compensation = kMaxOverpourCompTicks;

  int32_t effective = nominal - compensation;
  const int32_t min_target = nominal / 2;
  if (effective < min_target) effective = min_target;
  return effective;
}
} // namespace

void counter_targets_reload_runtime_settings(app_config_t &config,
                                             app_state_t &state,
                                             int32_t valve_targets[NUM_VALVES],
                                             int32_t current_valve,
                                             int32_t overpour_ticks[NUM_VALVES],
                                             CounterRuntimeSettings &runtime) {
  int32_t steps = 0, enc = 0, flush_valve_ms = 0, flush_all_ms = 0;
  config_get_cached_pump_settings(&steps, &enc, &flush_valve_ms, &flush_all_ms);
  if (steps > 0) config.steps = static_cast<uint32_t>(steps);
  config.encoder = enc;
  state.encoder = enc;
  runtime.flush_valve_ms = flush_valve_ms;
  runtime.flush_all_ms = flush_all_ms;

  int32_t valve_offsets[NUM_VALVES] = {0};
  config_get_cached_valve_offsets(valve_offsets);
  for (int i = 0; i < NUM_VALVES; i++) {
    config.valve_offset[i] = valve_offsets[i];
  }

  int32_t dry_ms = 0, dry_min = 0;
  config_get_cached_dry_run(&dry_ms, &dry_min);
  if (dry_ms > 0) runtime.dry_run_timeout_ms = dry_ms;
  if (dry_min >= 0) runtime.dry_run_min_ticks = dry_min;

  counter_targets_refresh_all(config, state, overpour_ticks, current_valve, valve_targets);
}

void counter_targets_refresh_one(const app_config_t &config,
                                 app_state_t &state,
                                 const int32_t overpour_ticks[NUM_VALVES],
                                 int32_t valve_idx0,
                                 int32_t valve_targets[NUM_VALVES]) {
  valve_idx0 = clamp_valve_idx(valve_idx0);
  const int32_t nominal = counter_targets_nominal(config, state, valve_idx0);
  const int32_t effective = effective_target_ticks(config, state, overpour_ticks, valve_idx0);
  valve_targets[valve_idx0] = effective;
  state.previous_target = nominal;
  state.water_target = effective;
}

void counter_targets_refresh_all(const app_config_t &config,
                                 app_state_t &state,
                                 const int32_t overpour_ticks[NUM_VALVES],
                                 int32_t current_valve,
                                 int32_t valve_targets[NUM_VALVES]) {
  for (int i = 0; i < NUM_VALVES; i++) {
    valve_targets[i] = effective_target_ticks(config, state, overpour_ticks, i);
  }

  const int idx0 = (current_valve >= 1 && current_valve <= NUM_VALVES) ? (current_valve - 1) : 0;
  state.previous_target = counter_targets_nominal(config, state, idx0);
  state.water_target = valve_targets[idx0];
}

int32_t counter_targets_nominal(const app_config_t &config,
                                const app_state_t &state,
                                int32_t valve_idx0) {
  valve_idx0 = clamp_valve_idx(valve_idx0);
  return current_base_target_ticks(config, state) + config.valve_offset[valve_idx0];
}

void counter_targets_update_overpour(int32_t overpour_ticks[NUM_VALVES],
                                     int32_t valve_idx0,
                                     int32_t overshoot_ticks) {
  valve_idx0 = clamp_valve_idx(valve_idx0);
  if (overshoot_ticks < 0) overshoot_ticks = 0;
  if (overshoot_ticks > kMaxOverpourCompTicks) overshoot_ticks = kMaxOverpourCompTicks;

  overpour_ticks[valve_idx0] = (overpour_ticks[valve_idx0] * 3 + overshoot_ticks) / 4;
}
