#ifndef TELEGRAM_CONFIG_H
#define TELEGRAM_CONFIG_H

// Конфигурация Telegram бота
// Проверяем, что токен не содержит лишних символов
#define TELEGRAM_BOT_TOKEN "5039588685:AAFrqiYBHZvENSFfaDBOH6tQ-JAPs4mCgN8"
#define TELEGRAM_CHAT_ID "-1002504369938"
#define TELEGRAM_MESSAGE_THREAD_ID "154"

// Иконка для идентификации наливайки (можно менять для разных устройств)
#define TELEGRAM_DEVICE_ICON "🚰"

// URL для отправки сообщений в Telegram (без лишнего слеша)
#define TELEGRAM_API_URL "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN "/sendMessage"

// Отладочная информация
#define TELEGRAM_DEBUG_INFO "Bot Token Length: " STRINGIFY(sizeof(TELEGRAM_BOT_TOKEN))

#endif // TELEGRAM_CONFIG_H 
