#include "telegram_manager.h"
#include "telegram_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "TELEGRAM_MANAGER";

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
        default:
            break;
    }
    return ESP_OK;
}

// Инициализация Telegram менеджера
esp_err_t telegram_init(void)
{
    ESP_LOGI(TAG, "Telegram manager initialized");
    return ESP_OK;
}

// Отправка сообщения в Telegram
esp_err_t telegram_send_message(const char* message)
{
    if (!message) {
        ESP_LOGE(TAG, "Message is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Создаем JSON для отправки
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "chat_id", TELEGRAM_CHAT_ID);
    cJSON_AddStringToObject(json, "text", message);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }

    // Настройка HTTP клиента
    esp_http_client_config_t config = {
        .url = TELEGRAM_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
    };

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
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_string);
    
    return err;
}

// Отправка уведомления о подключении к WiFi
esp_err_t telegram_send_wifi_connected(void)
{
    const char* message = "🔌 Устройство подключилось к WiFi сети";
    return telegram_send_message(message);
}

// Отправка уведомления о нажатии кнопки
esp_err_t telegram_send_button_press(const char* button_name)
{
    char message[256];
    snprintf(message, sizeof(message), "🔘 Нажата кнопка: %s", button_name);
    return telegram_send_message(message);
}

// Отправка уведомления о состоянии устройства
esp_err_t telegram_send_device_status(const char* status)
{
    char message[256];
    snprintf(message, sizeof(message), "📊 Статус устройства: %s", status);
    return telegram_send_message(message);
} 
