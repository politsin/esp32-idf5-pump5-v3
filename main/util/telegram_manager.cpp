#include "telegram_manager.h"
#include "telegram_config.h"
#include "telegramTask.h"
#include "esp_log.h"

static const char *TAG = "TELEGRAM_MANAGER";

// Инициализация Telegram менеджера
esp_err_t telegram_init(void)
{
    ESP_LOGI(TAG, "Initializing Telegram manager...");
    
    // Инициализируем очередь сообщений
    esp_err_t result = telegram_queue_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize telegram queue");
        return result;
    }
    
    ESP_LOGI(TAG, "Telegram manager initialized successfully");
    ESP_LOGI(TAG, "Waiting 2 seconds before continuing...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    return ESP_OK;
}

// Тестовая функция для проверки бота
esp_err_t telegram_test_bot(void)
{
    ESP_LOGI(TAG, "Testing bot connection...");
    // Тестовая функция пока не реализована
    return ESP_OK;
}

// Отправка сообщения в Telegram
esp_err_t telegram_send_message(const char* message)
{
    return telegram_send_message_async(message);
}

// Отправка уведомления о подключении к WiFi
esp_err_t telegram_send_wifi_connected(void)
{
    ESP_LOGI(TAG, "Sending WiFi connected notification...");
    const char* message = "🫵 Наливайка подключилась к WiFi сети";
    esp_err_t result = telegram_send_message_async(message);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected notification sent successfully");
    } else {
        ESP_LOGE(TAG, "Failed to send WiFi connected notification");
    }
    return result;
}

// Отправка уведомления о нажатии кнопки
esp_err_t telegram_send_button_press(const char* button_name)
{
    char message[256];
    snprintf(message, sizeof(message), "Нажата кнопка: %s", button_name);
    return telegram_send_message_async(message);
}

// Отправка уведомления о нажатии кнопки с иконкой
esp_err_t telegram_send_button_press_with_icon(const char* icon, const char* button_name)
{
    char message[256];
    snprintf(message, sizeof(message), "%s Нажата кнопка: %s", icon, button_name);
    return telegram_send_message_async(message);
}

// Отправка уведомления о состоянии устройства
esp_err_t telegram_send_device_status(const char* status)
{
    char message[256];
    snprintf(message, sizeof(message), "Статус наливайки: %s", status);
    return telegram_send_message_async(message);
}

// Отправка отчёта о завершении работы
esp_err_t telegram_send_completion_report(int32_t banks_count, int32_t total_time_ticks)
{
    char message[512];
    int32_t total_seconds = total_time_ticks / 100;
    int32_t hours = total_seconds / 3600;
    int32_t minutes = (total_seconds % 3600) / 60;
    int32_t seconds = total_seconds % 60;
    
    snprintf(message, sizeof(message), 
             "🏁 Работа завершена!\n"
             "Налито банок: %ld\n"
             "Расход в литрах: %ld\n"
             "Время работы: %02ld:%02ld:%02ld",
             banks_count, banks_count / 4, hours, minutes, seconds);
    
    return telegram_send_message_async(message);
}

// Отправка промежуточного отчёта о прогрессе
esp_err_t telegram_send_progress_report(int32_t banks_count, int32_t current_time_ticks)
{
    char message[512];
    int32_t current_seconds = current_time_ticks / 100;
    int32_t minutes = current_seconds / 60;
    int32_t seconds = current_seconds % 60;
    
    snprintf(message, sizeof(message), 
             "🚰 Налив идёт!\n"
             "Налито банок: %ld\n"
             "Расход в литрах: %ld\n"
             "Время работы: %02ld:%02ld\n"
             "Льём дальше...",
             banks_count, banks_count / 4, minutes, seconds);
    
    return telegram_send_message_async(message);
} 
