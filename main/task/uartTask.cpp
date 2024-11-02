// #include <uartTask.h> WTF o.O
// Uart #22 #19.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include <esp_err.h>
#include <esp_log.h>
#include <hal/gpio_types.h>
#include <hal/uart_types.h>
#include <rom/gpio.h>
#include <stdio.h>

#include "main.h"
#include <iostream>
using std::string;
using std::to_string;

#define UART_TAG "UART"
#define UART_SEND "SEND"
#define BUF_SIZE (1024)

typedef gpio_num_t Pintype;
static constexpr Pintype RTS = GPIO_NUM_2;
static constexpr Pintype TX = GPIO_NUM_13;
static constexpr Pintype RX = GPIO_NUM_15;
// GND ↑ GND--13--15--2
static constexpr Pintype CTS = GPIO_NUM_19;
static int badu = 115200;
uart_port_t uart_no = 2;

TaskHandle_t uart;
void uartTask(void *pvParam) {
  const TickType_t xBlockTime = pdMS_TO_TICKS(300);
  uart_config_t uart_config = {
      .baud_rate = badu,
      .data_bits = UART_DATA_8_BITS,
      // .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 122,
  };

  int intr_alloc_flags = 0;
  ESP_ERROR_CHECK(
      uart_driver_install(uart_no, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
  ESP_ERROR_CHECK(uart_param_config(uart_no, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(uart_no, TX, RX, RTS, CTS));
  uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

  while (true) {
    if (true) {
      string msg = "BTN_ENC:" + std::to_string(app_data.btn_enc) +
                   " RED:" + std::to_string(app_data.btn_red) +
                   " BLUE:" + std::to_string(app_data.btn_blue) +
                   " ENC:" + std::to_string(app_data.encoder) +
                   " SCALE:" + std::to_string(app_data.scale) +
                   " RAW:" + std::to_string(app_data.raw) +
                   " XP:" + std::to_string(app_data.xp);
      // " XP:" + std::to_string(app_data.xp) + "\r\n";
      app_data.btn_enc = 0;
      app_data.btn_red = 0;
      app_data.btn_blue = 0;
      // ESP_LOGW(UART_TAG, "uart! %s", msg.c_str());
      uart_write_bytes(uart_no, msg.c_str(), msg.length());
      // ESP_LOGI(UART_SEND, "|=| %s", msg.c_str());
      printf("|=|%s|^|\n", msg.c_str());
      vTaskDelay(xBlockTime);
    }
    else {
      // Read data from the UART
      int len = uart_read_bytes(uart_no, data, (BUF_SIZE - 1),
                                20 / portTICK_PERIOD_MS);
      // Write data back to the UART
      uart_write_bytes(uart_no, (const char *)data, len);
      if (len) {
        data[len] = '\0';
        ESP_LOGI(UART_TAG, "Recv str: %s", (char *)data);
      }
    }
  }
}

// Float To String.
string scaleToSting(float hx) {
  char data[16];
  sprintf(data, "%.2f", hx);
  string metric = string(data);
  return metric;
}
