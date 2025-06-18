#ifndef TELEGRAM_MANAGER_H
#define TELEGRAM_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация Telegram менеджера
esp_err_t telegram_init(void);

// Отправка сообщения в Telegram
esp_err_t telegram_send_message(const char* message);

// Отправка уведомления о подключении к WiFi
esp_err_t telegram_send_wifi_connected(void);

// Отправка уведомления о нажатии кнопки
esp_err_t telegram_send_button_press(const char* button_name);

// Отправка уведомления о состоянии устройства
esp_err_t telegram_send_device_status(const char* status);

#ifdef __cplusplus
}
#endif

#endif // TELEGRAM_MANAGER_H 
