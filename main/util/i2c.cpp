#include <driver/i2c.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2C "I2C"
#define CONFIG_IOT_I2C "1"
#define CONFIG_IOT_I2C_SCAN "1"

#include "i2c.h"

static char s_i2c_last_scan_summary[96] = "i2c: --";

const char *i2c_last_scan_summary(void) {
  return s_i2c_last_scan_summary;
}

static void update_i2c_last_scan_summary(const uint8_t *addrs, size_t count) {
  size_t pos = 0;
  pos += (size_t)snprintf(s_i2c_last_scan_summary + pos, sizeof(s_i2c_last_scan_summary) - pos, "i2c:");
  if (count == 0 || pos >= sizeof(s_i2c_last_scan_summary)) {
    snprintf(s_i2c_last_scan_summary + pos, sizeof(s_i2c_last_scan_summary) - pos, " --");
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if (pos >= sizeof(s_i2c_last_scan_summary)) break;
    const int n = snprintf(s_i2c_last_scan_summary + pos, sizeof(s_i2c_last_scan_summary) - pos, " 0x%02X", (unsigned)addrs[i]);
    if (n <= 0) break;
    pos += (size_t)n;
  }
}

esp_err_t i2c_init(bool scan) {
#ifdef CONFIG_IOT_I2C
  static bool s_inited = false;
  if (s_inited) {
    ESP_LOGI(I2C, "I2C already initialized");
    if (scan) ESP_ERROR_CHECK(iot_i2c_scan(1));
    return ESP_OK;
  }
  // Важно: в app_main() для тега "I2C" выставлен уровень WARN, поэтому
  // ключевую инфу о шине печатаем через ESP_LOGW, чтобы она была видна всегда.
  ESP_LOGW(I2C, "I2C init: port=%d SDA=%d SCL=%d freq=%uHz scan=%d",
           (int)I2C_NUM_0, (int)I2C_SDA, (int)I2C_SCL, 400000u, (int)scan);
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
  ESP_LOGW(I2C, "I2C init OK (port=%d)", (int)I2C_NUM_0);
  s_inited = true;
  if (scan) {
    // Try To SCAN
    ESP_ERROR_CHECK(iot_i2c_scan(1));
    // Освободим драйвер после скана, чтобы i2cdev мог установить свой без конфликтов
    i2c_driver_delete(I2C_NUM_0);
    s_inited = false;
    ESP_LOGW(I2C, "I2C driver released after scan (handover to i2cdev), port=%d", (int)I2C_NUM_0);
  }
#else
  ESP_LOGW(IOT, "I2C OFF");
#endif
  return ESP_OK;
}

esp_err_t iot_i2c_scan(uint8_t i2c_scan_count = 1) {
#ifdef CONFIG_IOT_I2C_SCAN
  // Собираем список найденных адресов, чтобы показать на экране.
  uint8_t found_addrs[32];
  size_t found_n = 0;

  printf("I2C scan on port %d (SDA=%d SCL=%d)\n", (int)I2C_NUM_0, (int)I2C_SDA, (int)I2C_SCL);
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

      if (res == ESP_OK && found_n < (sizeof(found_addrs) / sizeof(found_addrs[0]))) {
        // не добавляем дубликаты (на случай нескольких проходов/ошибок)
        bool exists = false;
        for (size_t k = 0; k < found_n; k++) {
          if (found_addrs[k] == i) { exists = true; break; }
        }
        if (!exists) {
          found_addrs[found_n++] = i;
        }
      }
      i2c_cmd_link_delete(cmd);
    }
    printf("\n\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  update_i2c_last_scan_summary(found_addrs, found_n);
#endif
  return ESP_OK;
}

TaskHandle_t i2cScan;
void i2cScanTask(void *pvParam) {
  i2c_init(true);
  while (true) {
    iot_i2c_scan();
  }
}
