#ifndef TELEGRAM_TASK_H
#define TELEGRAM_TASK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Объявление задачи
extern TaskHandle_t telegramTaskHandle;

#ifdef __cplusplus
extern "C" {
#endif

// Объявление функции задачи
extern "C" void telegramTask(void *pvParameter);

// Инициализация очереди сообщений
esp_err_t telegram_queue_init(void);

// Неблокирующая отправка сообщения в Telegram
esp_err_t telegram_send_message_async(const char* message);

// Очистка очереди сообщений
esp_err_t telegram_clear_queue(void);

#ifdef __cplusplus
}
#endif

#endif // TELEGRAM_TASK_H 
