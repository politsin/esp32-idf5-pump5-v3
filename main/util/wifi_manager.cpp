#include "wifi_manager.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_config.h"
#include "telegram_manager.h"
#include "../task/telegramTask.h"

static const char *TAG = "WIFI_MANAGER";

// FreeRTOS event group для синхронизации
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Переменные для экспоненциальной задержки переподключения
static int32_t reconnect_attempts = 0;
static const int32_t max_reconnect_delay_ms = 15 * 60 * 1000; // 15 минут в миллисекундах
static const int32_t base_reconnect_delay_ms = 1000; // 1 секунда базовая задержка

// Функция для расчёта задержки переподключения
static int32_t calculate_reconnect_delay(void) {
    // Экспоненциальная задержка: base_delay * 2^attempts
    // Но не больше max_reconnect_delay_ms
    int32_t delay = base_reconnect_delay_ms;
    for (int i = 0; i < reconnect_attempts && delay < max_reconnect_delay_ms; i++) {
        delay *= 2;
    }
    
    if (delay > max_reconnect_delay_ms) {
        delay = max_reconnect_delay_ms;
    }
    
    return delay;
}

// Обработчик событий WiFi
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Connecting to AP...");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected from AP, attempt %ld", reconnect_attempts + 1);
    
    // Рассчитываем задержку перед переподключением
    int32_t delay_ms = calculate_reconnect_delay();
    ESP_LOGI(TAG, "Reconnecting in %ld ms (attempt %ld)", delay_ms, reconnect_attempts + 1);
    
    // Увеличиваем счётчик попыток
    reconnect_attempts++;
    
    // Очищаем бит подключения
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    
    // Создаём задачу для переподключения с задержкой
    xTaskCreate([](void* param) {
        vTaskDelay(pdMS_TO_TICKS(*(int32_t*)param));
        esp_wifi_connect();
        vTaskDelete(NULL);
    }, "wifi_reconnect", 2048, &delay_ms, 5, NULL);
    
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASSWORD);
    
    // Сбрасываем счётчик попыток при успешном подключении
    if (reconnect_attempts > 0) {
        ESP_LOGI(TAG, "WiFi reconnected after %ld attempts", reconnect_attempts);
        reconnect_attempts = 0;
    }
    
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    
    // Очищаем очередь Telegram при подключении к WiFi
    telegram_clear_queue();
  }
}

// Инициализация WiFi
esp_err_t wifi_init(void) {
  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init finished.");

  // Ждём подключения к WiFi с таймаутом 10 секунд
  const TickType_t wifi_timeout = pdMS_TO_TICKS(10000);
  EventBits_t bits =
      xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                          pdFALSE, pdFALSE, wifi_timeout);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID,
             WIFI_PASSWORD);
    return ESP_OK;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", WIFI_SSID,
             WIFI_PASSWORD);
    return ESP_FAIL;
  } else {
    ESP_LOGW(TAG, "WiFi connection timeout after 10 seconds, continuing anyway");
    return ESP_OK; // Возвращаем OK, чтобы программа продолжала работать
  }
}

// Функция для OTA обновления
esp_err_t ota_update(const char *url) {
  esp_http_client_config_t http_config = {
      .url = url,
      .cert_pem = NULL,
      .skip_cert_common_name_check = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = (const esp_http_client_config_t *)&http_config,
      .buffer_caps = MALLOC_CAP_DMA | MALLOC_CAP_8BIT,
  };

  esp_err_t ret = esp_https_ota(&ota_config);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "OTA update successful, restarting...");
    esp_restart();
  } else {
    ESP_LOGE(TAG, "OTA update failed!");
  }
  return ret;
}
