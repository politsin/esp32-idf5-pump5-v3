// TOF 0x39 - TOF AP3216
#include "sdkconfig.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/gpio.h>
#define TOF_TAG "TOF"

#define I2C_FREQ_HZ 1000000
#define I2C_SDA GPIO_NUM_26
#define I2C_SCL GPIO_NUM_27

// #define TOF_ADDRESS 0x39
#define TOF_ADDRESS 0x1e
#define TOF_CONTROL 0x00
#define TOF_START_PS 0x02     // Proximity
#define TOF_START_AMB 0x01    // Ambient
#define TOF_START_AMB_PS 0x03 // ps+amb

#define TOF_PS_LOW 0x0E   // Proximity Data LOW 6 bits
#define TOF_PS_HIGH 0x0F  // Proximity Data HIGH 6 bits
#define TOF_AMB_LOW 0x0C  // Ambient Data LOW 6 bits
#define TOF_AMB_HIGH 0x0D // Ambient Data HIGH 6 bits

#include "i2c.h"
#include "i2cdev.h"
#include "stepperTask.h"
#include <esp_timer.h>
#include <i2cdev.h>
#include <string.h>
typedef struct {
  i2c_dev_t i2c_dev;
  int16_t AC1;
  bool ps;
  bool amb;
} tof_t;

void tofTestConnection();
esp_err_t tofBegin(tof_t *tof);
void tofStart(tof_t *tof);
void tofStartAmbient(tof_t *tof);
void tofStartProximity(tof_t *tof);

uint16_t tofGetAmbient(tof_t *tof);
uint16_t tofGetProximity(tof_t *tof);

esp_err_t tofWrite(i2c_dev_t *dev, uint8_t cmd);
esp_err_t tofRead(i2c_dev_t *dev, uint8_t reg, int8_t *r);
esp_err_t tofRead16(i2c_dev_t *dev, uint8_t reg, int16_t *r);

void smoothData(const uint16_t *input, uint16_t *output, int arraySize,
                int windowSize);
int findFirstPeak(const uint16_t *data, int arraySize);

TaskHandle_t tof;
void tofTask(void *pvParam) {
  const TickType_t xBlockTime = pdMS_TO_TICKS(200);
  tof_t tof;
  memset(&tof, 0, sizeof(tof_t));
  tofBegin(&tof);
  uint16_t amb = 0;
  uint16_t prox = 0;
  tofStartProximity(&tof);
  // Создаем массив proxArr[] из 200 элементов uint16_t
  const int arraySize = 2000;
  uint16_t proxArr[arraySize];
  uint64_t startTicks = esp_timer_get_time();
  uint64_t endTicks = esp_timer_get_time();
  uint64_t executionTimeMs = 0;

  // dendo
  int32_t steps = 100 * 16;
  int32_t diff = 100 * 16;
  uint32_t speed = 3000;
  step.setSpeed(speed, 200, 200);
  static const TickType_t xOffTime = pdMS_TO_TICKS(250);
  while (true) {
    prox = tofGetProximity(&tof);
    ESP_LOGI(TOF_TAG, "%u", prox);
    vTaskDelay(xBlockTime);
  }
}

void old() {
  // if (false) {
  //   amb = tofGetAmbient(&tof);
  //   ESP_LOGI(TOF_TAG, "TOF abm= %u | prox= %u", amb, prox);
  //   // ESP_LOGI(TOF_TAG, "tof!");
  // }
  // // Заполняем массив значениями из tofGetProximity
  // startTicks = esp_timer_get_time();
  // step.enableMotor();
  // step.runPos(-steps);
  // vTaskDelay(pdMS_TO_TICKS(200));
  // memset(proxArr, 0, sizeof(proxArr));
  // for (int i = 0; i < arraySize; i++) {
  //   proxArr[i] = tofGetProximity(&tof);
  //   // vTaskDelay(pdMS_TO_TICKS(10)); // Небольшая задержка между чтениями
  // }
  // endTicks = esp_timer_get_time();
  // executionTimeMs = (endTicks - startTicks) / 1000;
  // // Выводим массив на печать
  // // ESP_LOGI(TOF_TAG, "Массив значений proxArr:");
  // for (int i = 0; i < arraySize; i++) {
  //   // ESP_LOGI(TOF_TAG, "proxArr[%d] = %u", i, proxArr[i]);
  // }
  // // Выводим время выполнения
  // ESP_LOGI(TOF_TAG, "Время выполнения цикла: %llu mk", executionTimeMs);
  // vTaskDelay(xOffTime + pdMS_TO_TICKS(steps * 1000 / speed));
  // step.disableMotor();
  // ESP_LOGI(TOF_TAG, "done");
  // prox = tofGetProximity(&tof);
  // // 1. Smooth the data
  // uint16_t smoothedProxArr[arraySize];
  // int windowSize = 25;
  // // Adjust this value for desired smoothing level (odd number)
  // smoothData(proxArr, smoothedProxArr, arraySize, windowSize);
  // for (int i = 0; i < arraySize; i++) {
  //   // ESP_LOGI(TOF_TAG, "proxArr[%d] = %u  \t | %u", i, proxArr[i],
  //   //          smoothedProxArr[i]);
  // }

  // // 2. Find the first peak
  // int peakIndex = findFirstPeak(smoothedProxArr, arraySize);
  // // Adjust threshold as needed

  // if (peakIndex != -1) {
  //   ESP_LOGI(TOF_TAG, "First corner detected at index: %d, Value: %u",
  //            peakIndex, smoothedProxArr[peakIndex]);
  //   if (peakIndex > 200) {
  //     step.enableMotor();
  //     step.runPos(diff);
  //     vTaskDelay(xOffTime + pdMS_TO_TICKS(diff * 1000 / speed));
  //     step.disableMotor();
  //   }
  // } else {
  //   ESP_LOGI(TOF_TAG, "No corner detected.");
  // }
}

// Function to smooth the data using a moving average filter
void smoothData(const uint16_t *input, uint16_t *output, int arraySize,
                int windowSize) {
  if (windowSize % 2 == 0) {
    ESP_LOGE(TOF_TAG, "Window size for smoothing must be odd.");
    return;
  }
  int halfWindowSize = windowSize / 2;
  for (int i = 0; i < arraySize; i++) {
    int sum = 0;
    int count = 0;
    for (int j = std::max(0, i - halfWindowSize);
         j <= std::min(arraySize - 1, i + halfWindowSize); j++) {
      sum += input[j];
      count++;
    }
    output[i] = sum / count;
  }
}

// Function to find the first peak in the smoothed data
int findFirstPeak(const uint16_t *data, int arraySize) {
  bool bottom = false;
  int min = 600;
  int max = 900;
  for (int i = 1; i < arraySize; i++) {
    if (data[i] < min) {
      bottom = true;
    }
    if (bottom && data[i] > max) {
      return i;
    }
  }
  return -1;
}

esp_err_t tofBegin(tof_t *tof) {
  CHECK_ARG(tof);
  tof->ps = false;
  tof->amb = false;
  tof->i2c_dev.port = I2C_NUM_0;
  tof->i2c_dev.addr = TOF_ADDRESS;
  tof->i2c_dev.cfg.sda_io_num = I2C_SDA;
  tof->i2c_dev.cfg.scl_io_num = I2C_SCL;
  tof->i2c_dev.cfg.master.clk_speed = I2C_FREQ_HZ;
  return i2c_dev_create_mutex(&tof->i2c_dev);
}
void tofStart(tof_t *tof) {
  tof->ps = true;
  tof->amb = true;
  tofWrite(&tof->i2c_dev, TOF_START_AMB_PS);
}
void tofStartAmbient(tof_t *tof) {
  tof->ps = false;
  tof->amb = true;
  tofWrite(&tof->i2c_dev, TOF_START_AMB);
};
void tofStartProximity(tof_t *tof) {
  tof->ps = true;
  tof->amb = false;
  tofWrite(&tof->i2c_dev, TOF_START_PS);
};

uint16_t tofGetAmbient(tof_t *tof) {
  uint16_t alsRaw = 0x0000;
  if (tof->amb) {
    int8_t lowByte, highByte;
    tofRead(&tof->i2c_dev, TOF_AMB_LOW, &lowByte);
    tofRead(&tof->i2c_dev, TOF_AMB_HIGH, &highByte);
    alsRaw = (highByte << 8) + lowByte;
  }
  return alsRaw;
};
uint16_t tofGetProximity(tof_t *tof) {
  uint16_t proximity = 0x0000;
  if (tof->ps) {
    int8_t lowByte, highByte;
    tofRead(&tof->i2c_dev, TOF_PS_LOW, &lowByte);
    tofRead(&tof->i2c_dev, TOF_PS_HIGH, &highByte);
    lowByte = lowByte & 0b00001111;
    highByte = highByte & 0b00111111;
    proximity = (highByte << 4) + lowByte;
  }
  return proximity;
};

esp_err_t tofWrite(i2c_dev_t *dev, uint8_t cmd) {
  return i2c_dev_write_reg(dev, TOF_CONTROL, &cmd, 1);
};
esp_err_t tofRead(i2c_dev_t *dev, uint8_t reg, int8_t *r) {
  return i2c_dev_read_reg(dev, reg, r, 1);
};
esp_err_t tofRead16(i2c_dev_t *dev, uint8_t reg, int16_t *r) {
  uint8_t d[] = {0, 0};
  CHECK(i2c_dev_read_reg(dev, reg, d, 2));
  *r = ((int16_t)d[0] << 8) | (d[1]);
  return ESP_OK;
}

// Old test
void tofTestConnection() {
  esp_err_t res;
  int addr = 0x39;
  // TOF? init
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, 1);
  i2c_master_stop(cmd);
  res = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
  if (res == 0)
    ESP_LOGI(TOF_TAG, "TOF [%.2x] - OK", addr);
  else
    ESP_LOGE(TOF_TAG, "TOF [%.2x] - FAIL", addr);
  i2c_cmd_link_delete(cmd);
}
