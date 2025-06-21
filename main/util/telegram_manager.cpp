#include "telegram_manager.h"
#include "telegram_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "TELEGRAM_MANAGER";

// Глобальные переменные для хранения ответа сервера
static char *server_response = NULL;
static int response_length = 0;

// HTTP обработчик для отправки сообщений
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Client Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP Client Connected");
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP Client Finished");
            break;
        case HTTP_EVENT_ON_DATA:
            // Получаем данные ответа
            if (evt->data_len > 0) {
                if (server_response) {
                    free(server_response);
                }
                server_response = (char *)malloc(evt->data_len + 1);
                if (server_response) {
                    memcpy(server_response, evt->data, evt->data_len);
                    server_response[evt->data_len] = '\0';
                    response_length = evt->data_len;
                    ESP_LOGI(TAG, "Received response: %s", server_response);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Инициализация Telegram менеджера
esp_err_t telegram_init(void)
{
    ESP_LOGI(TAG, "Telegram manager initialized");
    
    // Убираем автоматический тест бота, который вызывает ошибку 409
    // Бот будет протестирован при первой отправке сообщения
    
    return ESP_OK;
}

// Тестовая функция для проверки бота
esp_err_t telegram_test_bot(void)
{
    ESP_LOGI(TAG, "Testing bot connection...");
    
    // Отладочная информация для теста
    ESP_LOGI(TAG, "Test URL: https://api.telegram.org/bot%s/getUpdates", TELEGRAM_BOT_TOKEN);
    
    // Настройка HTTP клиента для теста
    esp_http_client_config_t config = {};
    config.url = "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN "/getUpdates";
    config.method = HTTP_METHOD_GET;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.skip_cert_common_name_check = true;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    
    // Отключаем верификацию сертификатов
    config.cert_pem = NULL;
    config.client_cert_pem = NULL;
    config.client_key_pem = NULL;
    config.cert_len = 0;
    config.client_cert_len = 0;
    config.client_key_len = 0;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for test");
        return ESP_FAIL;
    }

    // Отправляем запрос
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Test request status: %d", status_code);
        
        // Проверяем, получили ли мы ответ через обработчик
        if (server_response && response_length > 0) {
            ESP_LOGI(TAG, "Bot test response: %s", server_response);
        } else {
            ESP_LOGE(TAG, "No response received from server");
        }
        
        if (status_code == 200) {
            ESP_LOGI(TAG, "Bot is working correctly!");
        } else {
            ESP_LOGE(TAG, "Bot test failed with status %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Bot test request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    
    // Очищаем ответ
    if (server_response) {
        free(server_response);
        server_response = NULL;
        response_length = 0;
    }
    
    return err;
}

// Отправка сообщения в Telegram
esp_err_t telegram_send_message(const char* message)
{
    if (!message) {
        ESP_LOGE(TAG, "Message is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Создаем сообщение с иконкой устройства
    char full_message[512];
    snprintf(full_message, sizeof(full_message), "%s %s", TELEGRAM_DEVICE_ICON, message);

    // Создаем JSON для отправки
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "chat_id", TELEGRAM_CHAT_ID);
    cJSON_AddStringToObject(json, "text", full_message);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }

    // Настройка HTTP клиента с SSL без верификации
    esp_http_client_config_t config = {};
    config.url = TELEGRAM_API_URL;
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    config.timeout_ms = 10000;
    config.skip_cert_common_name_check = true;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    
    // Отключаем верификацию сертификатов
    config.cert_pem = NULL;
    config.client_cert_pem = NULL;
    config.client_key_pem = NULL;
    config.cert_len = 0;
    config.client_cert_len = 0;
    config.client_key_len = 0;
    
    // Отладочная информация
    ESP_LOGI(TAG, "Bot Token: %s", TELEGRAM_BOT_TOKEN);
    ESP_LOGI(TAG, "Chat ID: %s", TELEGRAM_CHAT_ID);
    ESP_LOGI(TAG, "API URL: %s", TELEGRAM_API_URL);
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(json_string);
        return ESP_FAIL;
    }

    // Устанавливаем заголовки
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    // Отправляем запрос
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Message sent successfully");
        } else {
            ESP_LOGE(TAG, "HTTP request failed, status: %d", status_code);
            
            // Проверяем ответ сервера
            if (server_response && response_length > 0) {
                ESP_LOGE(TAG, "Server response: %s", server_response);
            } else {
                ESP_LOGE(TAG, "No server response received");
            }
            
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_string);
    
    // Очищаем ответ
    if (server_response) {
        free(server_response);
        server_response = NULL;
        response_length = 0;
    }
    
    return err;
}

// Отправка уведомления о подключении к WiFi
esp_err_t telegram_send_wifi_connected(void)
{
    const char* message = "Наливайка подключилась к WiFi сети";
    return telegram_send_message(message);
}

// Отправка уведомления о нажатии кнопки
esp_err_t telegram_send_button_press(const char* button_name)
{
    char message[256];
    snprintf(message, sizeof(message), "Нажата кнопка: %s", button_name);
    return telegram_send_message(message);
}

// Отправка уведомления о состоянии устройства
esp_err_t telegram_send_device_status(const char* status)
{
    char message[256];
    snprintf(message, sizeof(message), "Статус наливайки: %s", status);
    return telegram_send_message(message);
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
             "Время работы: %02ld:%02ld:%02ld",
             banks_count, hours, minutes, seconds);
    
    return telegram_send_message(message);
} 
