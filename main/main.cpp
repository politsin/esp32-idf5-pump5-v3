#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <rom/gpio.h>
#include <stdio.h>

#include "util/config.h"
#include "util/wifi_manager.h"
#include "util/telegram_manager.h"
#include <main.h>
#define MAINTAG "MAIN"

// tasks
#include "task/blinkTask.h"
#include "task/buttonTask.h"
#include "task/counterTask.h"
#include "task/encoderTask.h"
#include "task/screenTask.h"
#include "task/telegramTask.h"
#include "task/timeTask.h"

#include "i2cdev.h"
#include "util/i2c.h"
#include "util/pcf8575_io.h"
#include "pcf8575.h"

app_state_t app_state = {
    .is_on = false,
    .rock = false,
    .time = 0,
    .encoder = 0,
    .water_target = 0,
    .water_current = 0,
    .water_delta = 0,
    .freeHeap = 0,
    .valve = 0,
    .valve_times = {0, 0, 0, 0, 0}, // Инициализация времени клапанов
    .banks_count = 0, // Инициализация счётчика банок
    .total_banks_count = 0, // Инициализация общего счётчика банок
    .today_banks_count = 0, // Инициализация счётчика банок налитых сегодня
    .start_time = 0, // Инициализация времени старта
    .final_time = 0, // Инициализация финального времени
    .final_banks = 0, // Инициализация финального количества банок
    .counter_error = false, // Инициализация флага ошибки счётчика
    .previous_target = 0, // Инициализация предыдущей цели
};

extern TaskHandle_t timeTaskHandle;

extern "C" void app_main(void) {
  ESP_LOGI(MAINTAG, "=== APP MAIN STARTED ===");
  config_init();
  ESP_LOGI(MAINTAG, "Config initialized");
  
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  esp_log_level_set("wifi_init", ESP_LOG_WARN);
  esp_log_level_set("WIFI_MANAGER", ESP_LOG_WARN);
  esp_log_level_set("TELEGRAM_MANAGER", ESP_LOG_WARN);
  esp_log_level_set("gpio", ESP_LOG_WARN);
  // esp_log_level_set("BUTTON", ESP_LOG_WARN);
  // esp_log_level_set("COUNTER", ESP_LOG_WARN);
  esp_log_level_set("TFT", ESP_LOG_WARN);
  esp_log_level_set("ENCODER", ESP_LOG_WARN);
  ESP_LOGW(MAINTAG, "Hello world!!");
  uint32_t min = 768 + configSTACK_OVERHEAD_TOTAL;

  // ИНИЦИАЛИЗАЦИЯ I2C ДО ВСЕХ ТАСКОВ (и до WiFi/Telegram)
  i2c_init(true);
  ESP_ERROR_CHECK(i2cdev_init());
  {
    // ТЕСТ: мигать только P15 (помпа, бит 15) напрямую через pcf8575_* из esp-idf-lib
    i2c_dev_t dev = {};
    ESP_ERROR_CHECK(pcf8575_init_desc(&dev, PCF8575_I2C_ADDR_BASE, I2C_NUM_0, I2C_SDA, I2C_SCL));
    uint16_t port = 0xFFFF; // все OFF (активный низ: 1)
    ESP_ERROR_CHECK(pcf8575_port_write(&dev, port)); // погасить всё (1)
    uint32_t cycle = 0;
    // Последовательность: клапаны P12,P13,P14,P15 (бит 10..13), затем помпа P17 (бит 15)
    const int bits_seq[] = {10, 11, 12, 13, 15};
    while (true) {
      for (size_t i = 0; i < sizeof(bits_seq)/sizeof(bits_seq[0]); ++i) {
        int bit = bits_seq[i];
        // Пробуем probe с ACK
        esp_err_t prw = i2c_dev_probe(&dev, I2C_DEV_WRITE);
        esp_err_t prr = i2c_dev_probe(&dev, I2C_DEV_READ);
        // ON: активный низ -> 0 на выбранном бите, остальные 1
        uint16_t on_port = (uint16_t)(0xFFFF & ~(1u << bit));
        esp_err_t w1 = pcf8575_port_write(&dev, on_port);
        uint16_t rv1 = 0xFFFF;
        esp_err_t r1 = pcf8575_port_read(&dev, &rv1);
        ESP_LOGI(MAINTAG, "IOEXP OUT P%02d ON  write=%s read=%s val=0x%04x probeW=%s probeR=%s",
                 bit, esp_err_to_name(w1), esp_err_to_name(r1), (unsigned)rv1,
                 esp_err_to_name(prw), esp_err_to_name(prr));
        vTaskDelay(pdMS_TO_TICKS(300));
        // OFF: вернуть все 1
        esp_err_t w2 = pcf8575_port_write(&dev, 0xFFFF);
        uint16_t rv2 = 0xFFFF;
        esp_err_t r2 = pcf8575_port_read(&dev, &rv2);
        ESP_LOGI(MAINTAG, "IOEXP OUT P%02d OFF write=%s read=%s val=0x%04x",
                 bit, esp_err_to_name(w2), esp_err_to_name(r2), (unsigned)rv2);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      ESP_LOGI(MAINTAG, "IOEXP TEST seq valves(12..15)+pump(17) cycle %lu", (unsigned long)++cycle);
    }
  }

  // Инициализация WiFi
  ESP_LOGI(MAINTAG, "Starting WiFi initialization...");
  // Запускаем WiFi инициализацию в отдельной задаче, чтобы не блокировать основной поток
  xTaskCreate([](void* param) {
    esp_err_t wifi_result = wifi_init();
    if (wifi_result != ESP_OK) {
      ESP_LOGE(MAINTAG, "WiFi initialization failed, but continuing...");
    } else {
      ESP_LOGI(MAINTAG, "WiFi initialization successful");
    }
    vTaskDelete(NULL);
  }, "wifi_init", 4096, NULL, 1, NULL);
  ESP_LOGI(MAINTAG, "WiFi initialization task created");

  // Инициализация Telegram менеджера
  ESP_ERROR_CHECK(telegram_init());

  // tasks.
  ESP_LOGI(MAINTAG, "Creating tasks...");
  xTaskCreate(&loop, "loop", min * 3, NULL, 2, NULL);
  ESP_LOGI(MAINTAG, "Loop task created");
  // xTaskCreate(stepperTask, "stepper", min * 8, NULL, 1, &stepper);
  // xTaskCreate(tofTask, "tof", min * 32, NULL, 1, &tof);
  // xTaskCreate(blinkTask, "blink", min * 4, NULL, 1, &blink);
  xTaskCreate(buttonTask, "button", min * 4, NULL, 1, &button);
  ESP_LOGI(MAINTAG, "Button task created");
  xTaskCreate(counterTask, "counter", min * 10, NULL, 1, &counter);
  ESP_LOGI(MAINTAG, "Counter task created");
  xTaskCreate(encoderTask, "encoder", min * 6, NULL, 1, &encoder);
  ESP_LOGI(MAINTAG, "Encoder task created");
  xTaskCreatePinnedToCore(screenTask, "screen", min * 10, NULL, 1, &screen, 1);
  ESP_LOGI(MAINTAG, "Screen task created");
  xTaskCreate(telegramTask, "telegram", min * 8, NULL, 5, &telegramTaskHandle);
  ESP_LOGI(MAINTAG, "Telegram task created");
  xTaskCreate(timeTask, "time", min * 10, NULL, 1, &timeTaskHandle);
  ESP_LOGI(MAINTAG, "Time task created");
  ESP_LOGI(MAINTAG, "All tasks created successfully");
  // Отправляем уведомление о подключении к WiFi
  const TickType_t xBlockTime = pdMS_TO_TICKS(5 * 1000);
  ESP_LOGI(MAINTAG, "Waiting 5 seconds...");
  vTaskDelay(xBlockTime);
  ESP_LOGI(MAINTAG, "5 seconds passed");
  // telegram_send_wifi_connected(); // Уведомление будет отправлено автоматически при подключении к WiFi
  ESP_LOGI(MAINTAG, "WiFi connected notification will be sent automatically");
  // xTaskCreatePinnedToCore(uartTask, "uart", min * 10, NULL, 1, &uart, 0);
  // xTaskCreate(hx711Task, "hx711", min * 16, NULL, 1, &hx711);
  // xTaskCreate(i2cScanTask, "i2cScan", min * 4, NULL, 5, &i2cScan);
  ESP_LOGI(MAINTAG, "=== APP MAIN FINISHED ===");
}

// Loop Task
void loop(void *pvParameter) {
  uint32_t size = 0;
  uint32_t count = 0;
  const TickType_t xBlockTime = pdMS_TO_TICKS(500 * 1000);
  while (1) {
    count++;
    if ((count % 100) == true) {
      size = xPortGetFreeHeapSize();
      app_state.freeHeap = size;
      ESP_LOGW(MAINTAG, "xP %ld", size);
      vTaskDelay(xBlockTime);
    }
  }
}
