#pragma once

#include "driver/gpio.h"
#include <esp_err.h>
#include <freertos/queue.h>
#include <freertos/task.h>

extern TaskHandle_t i2cScan;
void i2cScanTask(void *pvParam);

esp_err_t i2c_init(bool scan);
esp_err_t iot_i2c_scan(uint8_t i2c_scan_count);

#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

/**
 * I2C scan
 *
 *
 * Use:
 *  #include "util/i2c.h"
 *  xTaskCreate(i2cScanTask, "i2cScan", min * 4, NULL, 5, &i2cScan);
 *
 * 48 - EC ADC
 * 39 - TOF AP3216
 */
//      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
// 00:          -- -- -- -- -- -- -- -- -- -- -- -- --
// 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 30: -- -- -- -- -- -- -- -- -- 39 -- -- 3c -- -- --
// 40: -- -- -- -- 44 -- -- -- 48 -- -- -- -- -- -- --
// 50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// 70: -- -- -- -- -- -- -- 77
