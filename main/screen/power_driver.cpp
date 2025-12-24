#include "esp_err.h"
#include "driver/gpio.h"
#include "product_pins.h"
bool power_driver_init() {

  gpio_config_t poweron_gpio_config = {};
  poweron_gpio_config.pin_bit_mask = 1ULL << BOARD_POWERON;
  poweron_gpio_config.mode = GPIO_MODE_OUTPUT;
  poweron_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
  poweron_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  poweron_gpio_config.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&poweron_gpio_config));
  gpio_set_level(BOARD_POWERON, 1);

  return true;
}
