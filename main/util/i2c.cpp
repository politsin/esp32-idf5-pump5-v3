#include <driver/i2c.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2C "I2C"
#define CONFIG_IOT_I2C "1"
#define CONFIG_IOT_I2C_SCAN "1"

#include "i2c.h"

esp_err_t i2c_init(bool scan) {
#ifdef CONFIG_IOT_I2C
  static bool s_inited = false;
  if (s_inited) {
    ESP_LOGI(I2C, "I2C already initialized");
    if (scan) ESP_ERROR_CHECK(iot_i2c_scan(1));
    return ESP_OK;
  }
  ESP_LOGI(I2C, "I2C init. SDA=%d SCL=%d", I2C_SDA, I2C_SCL);
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = I2C_SDA;
  conf.scl_io_num = I2C_SCL;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE; // используем внешние подтяжки на шине
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.clk_flags = 0;
  conf.master.clk_speed = 400000; // 400 кГц, совпадает с esp-idf-lib (pcf8575)
  i2c_param_config(I2C_NUM_0, &conf);
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
  ESP_LOGI(I2C, "I2C init OK");
  s_inited = true;
  if (scan) {
    // Try To SCAN
    ESP_ERROR_CHECK(iot_i2c_scan(1));
  }
#else
  ESP_LOGW(IOT, "I2C OFF");
#endif
  return ESP_OK;
}

esp_err_t iot_i2c_scan(uint8_t i2c_scan_count = 1) {
#ifdef CONFIG_IOT_I2C_SCAN
  for (uint8_t j = 0; j < i2c_scan_count; j++) {
    esp_err_t res;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:         ");
    for (uint8_t i = 3; i < 0x78; i++) {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, 1);
      i2c_master_stop(cmd);
      res = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
      if (i % 16 == 0)
        printf("\n%.2x:", i);
      if (res == 0)
        printf(" %.2x", i);
      else
        printf(" --");
      i2c_cmd_link_delete(cmd);
    }
    printf("\n\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
#endif
  return ESP_OK;
}

TaskHandle_t i2cScan;
void i2cScanTask(void *pvParam) {
  i2c_init(true);
  static const TickType_t xBlockTime = pdMS_TO_TICKS(50);
  while (true) {
    iot_i2c_scan();
  }
}
