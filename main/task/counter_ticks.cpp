#include "counter_ticks.h"

#include "counterTask.h"

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "config.h"

namespace {
static const char *TAG = "COUNTER";
static constexpr gpio_num_t DI = COUNTER_TICK_GPIO;

static volatile int32_t s_gpio_ticks_pending = 0;
static portMUX_TYPE s_gpio_ticks_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_tick_min_cycles = 0;
static volatile uint32_t s_tick_last_cycle = 0;

static void IRAM_ATTR counter_gpio_isr(void *arg) {
  (void)arg;
  portENTER_CRITICAL_ISR(&s_gpio_ticks_mux);
  if (s_tick_min_cycles != 0) {
    const uint32_t now = esp_cpu_get_cycle_count();
    const uint32_t dt = static_cast<uint32_t>(now - s_tick_last_cycle);
    if (dt < s_tick_min_cycles) {
      portEXIT_CRITICAL_ISR(&s_gpio_ticks_mux);
      return;
    }
    s_tick_last_cycle = now;
  }
  s_gpio_ticks_pending = s_gpio_ticks_pending + 1;
  portEXIT_CRITICAL_ISR(&s_gpio_ticks_mux);
}
} // namespace

void counter_ticks_init(CounterTickSource &source) {
  source.pcnt_unit = nullptr;
  source.pcnt_chan = nullptr;
  source.pcnt_ready = false;

  int32_t tick_source = 1;
  int32_t tick_min_us = 0;
  int32_t tick_pull = 0;
  config_get_cached_tick_counter(&tick_source, &tick_min_us, &tick_pull);
  if (tick_pull == 0) {
    tick_pull = 1;
    ESP_LOGW(TAG, "DI pull mode not configured, forcing PULL-UP");
  }

  if (tick_min_us < 0) tick_min_us = 0;
  if (tick_min_us > 500000) tick_min_us = 500000;
  const uint32_t cycles_per_us = static_cast<uint32_t>(esp_rom_get_cpu_ticks_per_us());
  portENTER_CRITICAL(&s_gpio_ticks_mux);
  s_tick_min_cycles = (tick_min_us > 0 && cycles_per_us > 0) ? (static_cast<uint32_t>(tick_min_us) * cycles_per_us) : 0U;
  s_tick_last_cycle = 0;
  s_gpio_ticks_pending = 0;
  portEXIT_CRITICAL(&s_gpio_ticks_mux);

  gpio_config_t di_config = {
      .pin_bit_mask = (1ULL << DI),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = (tick_pull == 1) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
      .pull_down_en = (tick_pull == 2) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&di_config);

  if (tick_source == 0) {
    source.pcnt_ready = true;
    pcnt_unit_config_t unit_cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
        .intr_priority = 0,
        .flags = {},
    };
    if (pcnt_new_unit(&unit_cfg, &source.pcnt_unit) != ESP_OK) {
      source.pcnt_ready = false;
    }

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = 10000,
    };
    if (source.pcnt_ready) {
      const esp_err_t filter_err = pcnt_unit_set_glitch_filter(source.pcnt_unit, &filter_cfg);
      if (filter_err != ESP_OK && filter_err != ESP_ERR_NOT_SUPPORTED) {
        source.pcnt_ready = false;
      }
    }

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = DI,
        .level_gpio_num = -1,
        .flags = {},
    };
    if (source.pcnt_ready && pcnt_new_channel(source.pcnt_unit, &chan_cfg, &source.pcnt_chan) != ESP_OK) {
      source.pcnt_ready = false;
    }
    if (source.pcnt_ready && pcnt_channel_set_edge_action(
                                 source.pcnt_chan,
                                 PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE) != ESP_OK) {
      source.pcnt_ready = false;
    }
    if (source.pcnt_ready && pcnt_channel_set_level_action(
                                 source.pcnt_chan,
                                 PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                 PCNT_CHANNEL_LEVEL_ACTION_KEEP) != ESP_OK) {
      source.pcnt_ready = false;
    }
    if (source.pcnt_ready) {
      pcnt_unit_enable(source.pcnt_unit);
      pcnt_unit_clear_count(source.pcnt_unit);
      pcnt_unit_start(source.pcnt_unit);
    }
  }

  if (!source.pcnt_ready) {
    const esp_err_t isr_res = gpio_install_isr_service(0);
    if (isr_res != ESP_OK && isr_res != ESP_ERR_INVALID_STATE) {
    }
    gpio_set_intr_type(DI, GPIO_INTR_NEGEDGE);
    (void)gpio_isr_handler_remove(DI);
    gpio_isr_handler_add(DI, counter_gpio_isr, nullptr);
    gpio_intr_enable(DI);
    ESP_LOGW(TAG, "Ticks: source=GPIO_ISR (NEGEDGE) debounce_us=%ld", static_cast<long>(tick_min_us));
  } else {
    ESP_LOGW(TAG, "Ticks: source=PCNT (glitch_filter_ns=10000)");
  }
}

int32_t counter_ticks_take_pending_gpio() {
  int32_t pending_ticks = 0;
  portENTER_CRITICAL(&s_gpio_ticks_mux);
  pending_ticks = s_gpio_ticks_pending;
  s_gpio_ticks_pending = 0;
  portEXIT_CRITICAL(&s_gpio_ticks_mux);
  return pending_ticks;
}

bool counter_ticks_try_take_pcnt(CounterTickSource &source, int32_t &delta_ticks) {
  delta_ticks = 0;
  if (!source.pcnt_ready || source.pcnt_unit == nullptr) return false;

  int pcnt_val = 0;
  if (pcnt_unit_get_count(source.pcnt_unit, &pcnt_val) != ESP_OK || pcnt_val == 0) return false;

  pcnt_unit_clear_count(source.pcnt_unit);
  delta_ticks = static_cast<int32_t>(pcnt_val);
  return true;
}

void counter_ticks_reset(CounterTickSource &source) {
  portENTER_CRITICAL(&s_gpio_ticks_mux);
  s_gpio_ticks_pending = 0;
  s_tick_last_cycle = 0;
  portEXIT_CRITICAL(&s_gpio_ticks_mux);

  if (source.pcnt_ready && source.pcnt_unit) {
    (void)pcnt_unit_clear_count(source.pcnt_unit);
  }
}
