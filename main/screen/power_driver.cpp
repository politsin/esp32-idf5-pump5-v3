#include "esp_err.h"
#include "esp_log.h"
#include <cstring>
#include <stdio.h>
static const char *TAG = "POWER";
#include "driver/gpio.h"
#include "product_pins.h"
bool power_driver_init() {

  // ESP_LOGI(TAG, "Turn on board power pin");
  gpio_config_t poweron_gpio_config = {0};
  poweron_gpio_config.pin_bit_mask = 1ULL << BOARD_POWERON;
  poweron_gpio_config.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&poweron_gpio_config));
  gpio_set_level(BOARD_POWERON, 1);

  return true;
}
