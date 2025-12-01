#include "telegramTask.h"
#include "telegram_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "TELEGRAM_TASK";

// Глобальные переменные для HTTP ответа
static char *server_response = NULL;
static size_t response_length = 0;

// Очередь для асинхронной отправки сообщений
static QueueHandle_t telegram_queue = NULL;
TaskHandle_t telegramTaskHandle = NULL;

// Структура для сообщения в очереди
typedef struct {
    char message[512];
    char chat_id[32];    // optional: пусто -> использовать дефолт
    char thread_id[16];  // optional: пусто -> использовать дефолт
    uint8_t important;   // 1 = продублировать в важный чат
} telegram_message_t;

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

// Проверка подключения к WiFi
static bool is_wifi_connected(void)
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

// Отправка сообщения в Telegram
static esp_err_t telegram_send_message_internal(const telegram_message_t* m)
{
    if (!m || !m->message[0]) {
        ESP_LOGE(TAG, "Message is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Создаем сообщение с иконкой устройства (избегаем -Wformat-truncation)
    char full_message[512];
    int n = snprintf(full_message, sizeof(full_message), "%s ", TELEGRAM_DEVICE_ICON);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(full_message)) n = (int)sizeof(full_message) - 1;
    size_t avail = sizeof(full_message) - (size_t)n;
    // Пишем не более avail-1 символов из текста
    snprintf(full_message + n, avail, "%.*s", (int)avail - 1, m->message);

    // Создаем JSON для отправки через прокси
    // Формат: { "channel": "...", "text": "...", "thread_id": 2 }
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "channel", (m->chat_id[0] ? m->chat_id : TELEGRAM_CHAT_ID));
    cJSON_AddStringToObject(json, "text", full_message);
    // thread_id как число, если строка содержит цифры
    const char* thr = (m->thread_id[0] ? m->thread_id : TELEGRAM_MESSAGE_THREAD_ID);
    int thr_num = 0;
    for (const char* p = thr; *p; ++p) { if (*p < '0' || *p > '9') { thr_num = -1; break; } thr_num = thr_num * 10 + (*p - '0'); }
    if (thr_num >= 0) cJSON_AddNumberToObject(json, "thread_id", thr_num);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }

    // Настройка HTTP клиента с SSL без верификации
    esp_http_client_config_t config = {};
    config.url = TELEGRAM_PROXY_URL;
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

// Задача для асинхронной отправки сообщений в Telegram
void telegramTask(void *pvParameter)
{
    ESP_LOGI(TAG, "Telegram task started");
    
    // Ждём инициализации очереди
    while (!telegram_queue) {
        ESP_LOGW(TAG, "Waiting for telegram queue initialization...");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Telegram queue is ready, starting message processing");
    
    telegram_message_t msg;
    
    while (1) {
        // Ждём сообщение из очереди
        if (xQueueReceive(telegram_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Received message from queue: %s", msg.message);
            
            // Проверяем WiFi перед отправкой
            if (!is_wifi_connected()) {
                ESP_LOGW(TAG, "WiFi disconnected, message not sent: %s", msg.message);
                continue;
            }
            
            // Отправляем сообщение синхронно в обычный канал
            esp_err_t result = telegram_send_message_internal(&msg);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Message sent successfully: %s", msg.message);
            } else {
                ESP_LOGE(TAG, "Failed to send message: %s", msg.message);
            }
            // При необходимости дублируем в важный канал
            if (msg.important) {
                telegram_message_t imp = msg;
                // переопределяем на важный чат/тред
                strncpy(imp.chat_id, TELEGRAM_CHAT_ID_IMPORTANT, sizeof(imp.chat_id) - 1);
                imp.chat_id[sizeof(imp.chat_id) - 1] = '\0';
                strncpy(imp.thread_id, TELEGRAM_MESSAGE_THREAD_ID_IMPORTANT, sizeof(imp.thread_id) - 1);
                imp.thread_id[sizeof(imp.thread_id) - 1] = '\0';
                esp_err_t r2 = telegram_send_message_internal(&imp);
                if (r2 == ESP_OK) {
                    ESP_LOGI(TAG, "Important duplicate sent: %s", msg.message);
                } else {
                    ESP_LOGE(TAG, "Failed to send important duplicate: %s", msg.message);
                }
            }
        }
    }
}

// Инициализация очереди сообщений
esp_err_t telegram_queue_init(void)
{
    ESP_LOGI(TAG, "Initializing Telegram queue...");
    
    // Создаём очередь для сообщений
    telegram_queue = xQueueCreate(10, sizeof(telegram_message_t)); // 10 сообщений
    if (!telegram_queue) {
        ESP_LOGE(TAG, "Failed to create telegram queue");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Telegram queue initialized successfully");
    return ESP_OK;
}

// Неблокирующая отправка сообщения в Telegram (обычный)
esp_err_t telegram_send_message_async(const char* message)
{
    if (!message || !telegram_queue) {
        ESP_LOGE(TAG, "Message is NULL or queue not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    // Проверяем подключение к WiFi
    if (!is_wifi_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, message not sent: %s", message);
        return ESP_FAIL;
    }

    telegram_message_t msg = {}; // по умолчанию normal
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0'; // Гарантируем завершающий ноль

    // Проверяем, есть ли место в очереди
    if (uxQueueMessagesWaiting(telegram_queue) >= 10) {
        ESP_LOGW(TAG, "Telegram queue is full, message dropped: %s", message);
        return ESP_FAIL;
    }

    // Копируем сообщение в очередь
    if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send message to queue");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Message queued for sending: %s", message);
    return ESP_OK;
}

// Неблокирующая отправка сообщения в конкретный чат/тред
esp_err_t telegram_send_message_async_to(const char* message, const char* chat_id, const char* thread_id)
{
    if (!message || !telegram_queue) {
        ESP_LOGE(TAG, "Message is NULL or queue not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_wifi_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, message not sent: %s", message);
        return ESP_FAIL;
    }
    telegram_message_t msg = {};
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    if (chat_id && chat_id[0]) {
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        msg.chat_id[sizeof(msg.chat_id) - 1] = '\0';
    }
    if (thread_id && thread_id[0]) {
        strncpy(msg.thread_id, thread_id, sizeof(msg.thread_id) - 1);
        msg.thread_id[sizeof(msg.thread_id) - 1] = '\0';
    }
    if (uxQueueMessagesWaiting(telegram_queue) >= 10) {
        ESP_LOGW(TAG, "Telegram queue is full, message dropped: %s", message);
        return ESP_FAIL;
    }
    if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send message to queue");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Message queued for sending to %s/%s: %s",
             (msg.chat_id[0] ? msg.chat_id : TELEGRAM_CHAT_ID),
             (msg.thread_id[0] ? msg.thread_id : TELEGRAM_MESSAGE_THREAD_ID),
             message);
    return ESP_OK;
}

// Неблокирующая отправка с признаком важности
esp_err_t telegram_send_message_async_flag(const char* message, bool important)
{
    if (!message || !telegram_queue) {
        ESP_LOGE(TAG, "Message is NULL or queue not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_wifi_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, message not sent: %s", message);
        return ESP_FAIL;
    }
    telegram_message_t msg = {};
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.important = important ? 1 : 0;
    if (uxQueueMessagesWaiting(telegram_queue) >= 10) {
        ESP_LOGW(TAG, "Telegram queue is full, message dropped: %s", message);
        return ESP_FAIL;
    }
    if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send message to queue");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Message queued (%s): %s", important ? "important" : "normal", message);
    return ESP_OK;
}

// Очистка очереди сообщений
esp_err_t telegram_clear_queue(void)
{
    if (!telegram_queue) {
        ESP_LOGE(TAG, "Telegram queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Очищаем очередь
    xQueueReset(telegram_queue);
    ESP_LOGI(TAG, "Telegram queue cleared");
    return ESP_OK;
} 
