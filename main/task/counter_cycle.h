#pragma once

#include "main.h"
#include "counter_targets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
                                   TaskHandle_t counter_task);

bool counter_check_dry_run(bool &is_on,
                           bool &pump_on,
                           TickType_t &pump_start_time,
                           int32_t pump_start_counter,
                           int32_t rot,
                           int32_t &current_valve,
                           TickType_t &valve_start_time,
                           const CounterRuntimeSettings &runtime);
