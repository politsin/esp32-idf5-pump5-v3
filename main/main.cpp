#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <rom/gpio.h>
#include <stdio.h>

#include "util/config.h"
#include "util/utility.h"
#include "util/web_log.h"
#include "util/spiffs_fs.h"
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
    .valve_times = {0, 0, 0, 0}, // Инициализация времени клапанов
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
  web_log_init();          // перехват логов для веб-журнала
  (void)spiffs_fs_mount(); // статика веб-морды из storage (SPIFFS)
  chip_info();             // печать инфо о чипе/flash
  config_init();
  ESP_LOGI(MAINTAG, "Config initialized");
  
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  esp_log_level_set("wifi_init", ESP_LOG_WARN);
  esp_log_level_set("WIFI_MANAGER", ESP_LOG_WARN);
  esp_log_level_set("TELEGRAM_MANAGER", ESP_LOG_WARN);
  esp_log_level_set("TELEGRAM_TASK", ESP_LOG_WARN);
  esp_log_level_set("I2C", ESP_LOG_WARN);
  esp_log_level_set("IOEXP", ESP_LOG_WARN);
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
    // Инициализация расширителя портов
    esp_err_t err = ioexp_init();
    if (err != ESP_OK) {
      ESP_LOGE(MAINTAG, "PCF8575 init failed (0x%x). Продолжаем без расширителя.", (unsigned)err);
    }
  }

  xTaskCreatePinnedToCore(screenTask, "screen", min * 10, NULL, 1, &screen, 1);
  ESP_LOGI(MAINTAG, "Screen task created");

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
  // Энкодера пока нет — задачу не запускаем
  // xTaskCreate(encoderTask, "encoder", min * 6, NULL, 1, &encoder);
  ESP_LOGI(MAINTAG, "Encoder task skipped (no hardware yet)");

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
