#ifndef TELEGRAM_CONFIG_H
#define TELEGRAM_CONFIG_H

// Конфигурация Telegram (через прокси)
// Текущий канал (через прокси используем алиас 'log')
#define TELEGRAM_CHAT_ID "log"
// ID треда/топика (передаём числом в прокси)
#define TELEGRAM_MESSAGE_THREAD_ID "2"

// Важный канал (через прокси используем алиас 'proxy')
#define TELEGRAM_CHAT_ID_IMPORTANT "proxy"
#define TELEGRAM_MESSAGE_THREAD_ID_IMPORTANT "154"

// Канальные алиасы для прокси:
// 'politsin' => 70721939  (alias: log)
#define TELEGRAM_CHANNEL_LOG "log"
// 'lera'     => 155154111 (alias: office)
#define TELEGRAM_CHANNEL_OFFICE "office"

// Иконка для идентификации наливайки (можно менять для разных устройств)
#define TELEGRAM_DEVICE_ICON "🚰"

// Прокси-эндпоинт (Яндекс Функция) для отправки сообщений
#define TELEGRAM_PROXY_URL "https://functions.yandexcloud.net/d4ejjat9rer6gghjvpbl"

#endif // TELEGRAM_CONFIG_H 
