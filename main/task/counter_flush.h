#pragma once

#include <stdbool.h>
#include <stdint.h>

bool counter_run_flush_sequence(bool &is_on,
                                bool &pump_on,
                                bool &flush_mode,
                                int32_t flush_valve_ms,
                                int32_t flush_all_ms);
