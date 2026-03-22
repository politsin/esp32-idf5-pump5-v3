#pragma once

#include <stdint.h>

#include "driver/pulse_cnt.h"

struct CounterTickSource {
  pcnt_unit_handle_t pcnt_unit;
  pcnt_channel_handle_t pcnt_chan;
  bool pcnt_ready;
};

void counter_ticks_init(CounterTickSource &source);
int32_t counter_ticks_take_pending_gpio();
bool counter_ticks_try_take_pcnt(CounterTickSource &source, int32_t &delta_ticks);
void counter_ticks_reset(CounterTickSource &source);
