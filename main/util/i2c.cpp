#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2C "I2C"
#define CONFIG_IOT_I2C "1"
#define CONFIG_IOT_I2C_SCAN "1"

#include "i2c.h"
#include "i2cdev.h"

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
  // Новый стек I2C (ESP-IDF >= 5.3): i2cdev сам поднимает i2c_master bus по первым операциям.
  // Здесь просто инициализируем i2cdev и (опционально) делаем scan через i2c_dev_probe().
  ESP_LOGW(I2C, "I2C init (new driver via i2cdev): port=%d SDA=%d SCL=%d freq=%uHz scan=%d",
           (int)I2C_NUM_0, (int)I2C_SDA, (int)I2C_SCL, 400000u, (int)scan);
  ESP_ERROR_CHECK(i2cdev_init());
  s_inited = true;
  if (scan) ESP_ERROR_CHECK(iot_i2c_scan(1));
#else
  ESP_LOGW(IOT, "I2C OFF");
#endif
  return ESP_OK;
}

esp_err_t iot_i2c_scan(uint8_t i2c_scan_count = 1) {
#ifdef CONFIG_IOT_I2C_SCAN
  // Скан через i2cdev_probe (внутри на новом драйвере использует i2c_master_probe).
  uint8_t found_addrs[32];
  size_t found_n = 0;

  printf("I2C scan (new driver) on port %d (SDA=%d SCL=%d)\n", (int)I2C_NUM_0, (int)I2C_SDA, (int)I2C_SCL);
  for (uint8_t j = 0; j < i2c_scan_count; j++) {
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:         ");

    for (uint8_t i = 3; i < 0x78; i++) {
      i2c_dev_t dev = {};
      dev.port = I2C_NUM_0;
      dev.addr = i;
      dev.addr_bit_len = I2C_ADDR_BIT_LEN_7;
      dev.cfg.sda_io_num = I2C_SDA;
      dev.cfg.scl_io_num = I2C_SCL;
      dev.cfg.sda_pullup_en = 0; // внешние подтяжки
      dev.cfg.scl_pullup_en = 0;
      dev.cfg.master.clk_speed = 400000;

      const esp_err_t res = i2c_dev_probe(&dev, I2C_DEV_WRITE);

      if (i % 16 == 0) printf("\n%.2x:", i);
      if (res == ESP_OK) printf(" %.2x", i);
      else printf(" --");

      if (res == ESP_OK && found_n < (sizeof(found_addrs) / sizeof(found_addrs[0]))) {
        bool exists = false;
        for (size_t k = 0; k < found_n; k++) {
          if (found_addrs[k] == i) { exists = true; break; }
        }
        if (!exists) found_addrs[found_n++] = i;
      }
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
