#ifndef TELEGRAM_MANAGER_H
#define TELEGRAM_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация Telegram менеджера
esp_err_t telegram_init(void);

// Тестовая функция для проверки бота
esp_err_t telegram_test_bot(void);

// Отправка сообщения в Telegram
esp_err_t telegram_send_message(const char* message);

// Отправка уведомления о подключении к WiFi
esp_err_t telegram_send_wifi_connected(void);

// Отправка уведомления о нажатии кнопки
esp_err_t telegram_send_button_press(const char* button_name);

// Отправка уведомления о нажатии кнопки с иконкой
esp_err_t telegram_send_button_press_with_icon(const char* icon, const char* button_name);

// Отправка уведомления о состоянии устройства
esp_err_t telegram_send_device_status(const char* status);

// Отправка отчёта о завершении работы
esp_err_t telegram_send_completion_report(int32_t banks_count, int32_t total_time_ticks);

// Отправка промежуточного отчёта о прогрессе
esp_err_t telegram_send_progress_report(int32_t banks_count, int32_t current_time_ticks);

#ifdef __cplusplus
}
#endif

#endif // TELEGRAM_MANAGER_H 
