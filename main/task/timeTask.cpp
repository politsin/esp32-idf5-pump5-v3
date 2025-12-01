// #include <timeTask.h> WTF o.O
// Time Task #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
#include <esp_log.h>
#include <rom/gpio.h>
#include <time.h>
#include "esp_sntp.h"
#include "esp_wifi.h"

#include <main.h>
#include <config.h>
#include "telegram_manager.h"

#define TIME_TAG "TIME"

// Константы для работы с временем
#define DAILY_REPORT_HOUR 18        // Час для отправки дневного отчёта (18:00)
#define TIME_CHECK_INTERVAL_MS 60000 // Проверка времени каждую минуту

TaskHandle_t timeTaskHandle;

// Переменные для отслеживания времени
static int last_report_day = -1;
static bool daily_report_sent = false;
static int32_t daily_banks_for_report = 0; // Значение дневного счётчика для отчёта

// Функция для синхронизации времени
static esp_err_t sync_time() {
    ESP_LOGI(TIME_TAG, "Starting time synchronization...");
    
    // Проверяем подключение к WiFi
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGW(TIME_TAG, "WiFi not connected, skipping time sync");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // Инициализируем SNTP если ещё не инициализирован
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_setservername(1, "time.nist.gov");
        sntp_init();
    }
    
    // Ждём синхронизации времени (максимум 10 секунд)
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TIME_TAG, "Waiting for time sync... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    if (retry < retry_count) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        ESP_LOGI(TIME_TAG, "Time sync successful: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        return ESP_OK;
    } else {
        ESP_LOGW(TIME_TAG, "Time sync failed");
        return ESP_ERR_TIMEOUT;
    }
}

// Функция для отправки дневного отчёта
static void send_daily_report() {
    if (daily_report_sent) {
        return; // Отчёт уже отправлен сегодня
    }
    
    ESP_LOGI(TIME_TAG, "Sending daily report...");
    
    char message[512];
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    snprintf(message, sizeof(message), 
             "📊 Дневной отчёт за %04d-%02d-%02d\n"
             "Налито сегодня: %ld банок\n"
             "Расход в литрах: %ld\n"
             "Всего налито за всё время: %ld банок",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             daily_banks_for_report, daily_banks_for_report / 4,
             app_state.total_banks_count);
    
    esp_err_t result = telegram_send_message(message);
    if (result == ESP_OK) {
        ESP_LOGI(TIME_TAG, "Daily report sent successfully");
        daily_report_sent = true;
        // Дублируем в старый канал/тред
        telegram_send_message_to(message, TELEGRAM_CHAT_ID_OLD, TELEGRAM_MESSAGE_THREAD_ID_OLD);
        
        // Синхронизируем время после отправки ежедневного отчёта
        ESP_LOGI(TIME_TAG, "Syncing time after daily report...");
        sync_time();
    } else {
        ESP_LOGW(TIME_TAG, "Failed to send daily report, will retry later");
    }
}

// Функция для проверки и сброса дневного счётчика
static void check_daily_reset() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Проверяем, новый ли это день
    if (timeinfo.tm_yday != last_report_day) {
        ESP_LOGI(TIME_TAG, "New day detected: %d, resetting daily report flag", timeinfo.tm_yday);
        
        // Сохраняем значение дневного счётчика для отчёта перед сбросом
        daily_banks_for_report = app_state.today_banks_count;
        ESP_LOGI(TIME_TAG, "Saved daily banks count for report: %ld", daily_banks_for_report);
        
        last_report_day = timeinfo.tm_yday;
        daily_report_sent = false;
        
        // Также проверяем сброс дневного счётчика
        check_and_reset_daily_counter();
    }
}

void timeTask(void *pvParam) {
    ESP_LOGI(TIME_TAG, "Time task started");
    
    // Синхронизируем время при инициализации задачи (при включении устройства)
    ESP_LOGI(TIME_TAG, "Initial time synchronization on device startup...");
    sync_time();
    
    // Инициализация времени
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    last_report_day = timeinfo.tm_yday;
    daily_banks_for_report = app_state.today_banks_count; // Инициализируем значение для отчёта
    
    const TickType_t xBlockTime = pdMS_TO_TICKS(TIME_CHECK_INTERVAL_MS);
    
    while (true) {
        // Получаем текущее время
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Проверяем сброс дневного счётчика
        check_daily_reset();
        
        // Проверяем, нужно ли отправить дневной отчёт (около 18:00)
        if (timeinfo.tm_hour == DAILY_REPORT_HOUR && !daily_report_sent) {
            send_daily_report();
        }
        
        // Логируем текущее время каждые 10 минут для отладки
        static int last_log_minute = -1;
        if (timeinfo.tm_min != last_log_minute && timeinfo.tm_min % 10 == 0) {
            ESP_LOGI(TIME_TAG, "Current time: %02d:%02d:%02d, Daily banks: %ld, Total banks: %ld",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     app_state.today_banks_count, app_state.total_banks_count);
            last_log_minute = timeinfo.tm_min;
        }
        
        vTaskDelay(xBlockTime);
    }
} 
