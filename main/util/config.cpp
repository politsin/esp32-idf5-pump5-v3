#include "config.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "time.h"

// nvs-C++
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"
#include <stdio.h>
#include <main.h>

#define CONFIG_TAG "CONFIG"
std::shared_ptr<nvs::NVSHandle> config;

bool config_reset = false;
int restart_counter = 0;

// Кэш уставок (загружается в config_init)
static int32_t s_steps = APP_DEFAULT_TARGET_TICKS; // цель в тиках (250мл по текущей калибровке)
static int32_t s_encoder = 0;           // смещение в тиках
static int32_t s_valve_offset[NUM_VALVES] = {0, 0, 0, 0}; // индивидуальные сдвиги по клапанам
static int32_t s_flush_valve_ms = 1000; // один клапан (мс)
static int32_t s_flush_all_ms = 2000;   // все клапаны (мс)
static int32_t s_dry_run_timeout_ms = 3000; // окно контроля "сухого хода" (мс) — по задаче 3 секунды
static int32_t s_dry_run_min_ticks = 50;    // минимальный прирост тиков за это окно
// Настройки счётчика тиков (DI)
static int32_t s_tick_source = 1;           // 0=PCNT, 1=GPIO ISR + debounce (по умолчанию — более надёжно на "грязном" сигнале)
static int32_t s_tick_min_interval_us = 0; // debounce выключен (считаем каждое прерывание)
static int32_t s_tick_pull = 1;            // 0=OFF, 1=PULL-UP, 2=PULL-DOWN (по умолчанию PULL-UP)
static char s_device_name[64] = {0};
static char s_telemetry_url[192] = {0};

// NVS ограничивает длину имени ключа 15 символами.
static constexpr const char *NVS_KEY_DRY_RUN_TIMEOUT_MS = "dry_ms";
static constexpr const char *NVS_KEY_DRY_RUN_MIN_TICKS = "dry_min";
static constexpr const char *NVS_KEY_TICK_MIN_INTERVAL_US = "tick_min_us";
static constexpr const char *NVS_KEY_LAST_RESET_ID = "last_reset_id";
static constexpr const char *NVS_KEY_LAST_RESET_DAY_LEGACY = "last_reset_day";
static constexpr const char *NVS_KEY_DEVICE_NAME = "dev_name";
static constexpr const char *NVS_KEY_TELEMETRY_URL = "telemetry_url";

static bool is_time_valid(const struct tm &timeinfo) {
  return timeinfo.tm_year >= (2024 - 1900);
}

static int32_t make_day_id(const struct tm &timeinfo) {
  return static_cast<int32_t>((timeinfo.tm_year + 1900) * 1000 + timeinfo.tm_yday);
}

void config_get_cached_pump_settings(int32_t *steps, int32_t *encoder, int32_t *flush_valve_ms, int32_t *flush_all_ms) {
  if (steps) *steps = s_steps;
  if (encoder) *encoder = s_encoder;
  if (flush_valve_ms) *flush_valve_ms = s_flush_valve_ms;
  if (flush_all_ms) *flush_all_ms = s_flush_all_ms;
}

void config_get_cached_telemetry(char *device_name, size_t device_name_len, char *telemetry_url, size_t telemetry_url_len) {
  if (device_name && device_name_len > 0) {
    snprintf(device_name, device_name_len, "%s", s_device_name);
  }
  if (telemetry_url && telemetry_url_len > 0) {
    snprintf(telemetry_url, telemetry_url_len, "%s", s_telemetry_url);
  }
}

esp_err_t config_save_telemetry(const char *device_name, const char *telemetry_url) {
  if (!config) return ESP_ERR_INVALID_STATE;

  s_device_name[0] = '\0';
  s_telemetry_url[0] = '\0';
  if (device_name) snprintf(s_device_name, sizeof(s_device_name), "%s", device_name);
  if (telemetry_url) snprintf(s_telemetry_url, sizeof(s_telemetry_url), "%s", telemetry_url);

  esp_err_t err = config->set_string(NVS_KEY_DEVICE_NAME, s_device_name);
  if (err != ESP_OK) return err;
  err = config->set_string(NVS_KEY_TELEMETRY_URL, s_telemetry_url);
  if (err != ESP_OK) return err;
  return config->commit();
}

void config_get_cached_valve_offsets(int32_t valve_offset[NUM_VALVES]) {
  if (!valve_offset) return;
  for (int i = 0; i < NUM_VALVES; i++) {
    valve_offset[i] = s_valve_offset[i];
  }
}

esp_err_t config_save_valve_offsets(const int32_t valve_offset[NUM_VALVES]) {
  if (!config) return ESP_ERR_INVALID_STATE;
  if (!valve_offset) return ESP_ERR_INVALID_ARG;

  for (int i = 0; i < NUM_VALVES; i++) {
    s_valve_offset[i] = valve_offset[i];
    char key[20];
    // valve_off1..valve_offN
    snprintf(key, sizeof(key), "valve_off%d", i + 1);
    esp_err_t err = config->set_item(key, s_valve_offset[i]);
    if (err != ESP_OK) return err;
  }
  return config->commit();
}

void config_get_cached_dry_run(int32_t *dry_run_timeout_ms, int32_t *dry_run_min_ticks) {
  if (dry_run_timeout_ms) *dry_run_timeout_ms = s_dry_run_timeout_ms;
  if (dry_run_min_ticks) *dry_run_min_ticks = s_dry_run_min_ticks;
}

esp_err_t config_save_dry_run(int32_t dry_run_timeout_ms, int32_t dry_run_min_ticks) {
  if (!config) return ESP_ERR_INVALID_STATE;

  s_dry_run_timeout_ms = dry_run_timeout_ms;
  s_dry_run_min_ticks = dry_run_min_ticks;

  esp_err_t err = config->set_item(NVS_KEY_DRY_RUN_TIMEOUT_MS, s_dry_run_timeout_ms);
  if (err != ESP_OK) return err;
  err = config->set_item(NVS_KEY_DRY_RUN_MIN_TICKS, s_dry_run_min_ticks);
  if (err != ESP_OK) return err;

  return config->commit();
}

void config_get_cached_tick_counter(int32_t *tick_source, int32_t *tick_min_interval_us, int32_t *tick_pull) {
  if (tick_source) *tick_source = s_tick_source;
  if (tick_min_interval_us) *tick_min_interval_us = s_tick_min_interval_us;
  if (tick_pull) *tick_pull = s_tick_pull;
}

esp_err_t config_save_tick_counter(int32_t tick_source, int32_t tick_min_interval_us, int32_t tick_pull) {
  if (!config) return ESP_ERR_INVALID_STATE;

  s_tick_source = tick_source;
  s_tick_min_interval_us = tick_min_interval_us;
  s_tick_pull = tick_pull;

  esp_err_t err = config->set_item("tick_source", s_tick_source);
  if (err != ESP_OK) return err;
  err = config->set_item(NVS_KEY_TICK_MIN_INTERVAL_US, s_tick_min_interval_us);
  if (err != ESP_OK) return err;
  err = config->set_item("tick_pull", s_tick_pull);
  if (err != ESP_OK) return err;

  return config->commit();
}

esp_err_t config_load_pump_settings(int32_t *steps, int32_t *encoder, int32_t *flush_valve_ms, int32_t *flush_all_ms) {
  if (!config) return ESP_ERR_INVALID_STATE;

  int32_t tmp = 0;

  // steps (цель в тиках)
  esp_err_t err = config->get_item("steps", tmp);
  if (err == ESP_OK) {
    s_steps = tmp;
    if (s_steps < 100 || s_steps > 500000) {
      ESP_LOGW(CONFIG_TAG,
               "Invalid steps=%ld found in NVS, resetting to default=%ld",
               (long)s_steps, (long)APP_DEFAULT_TARGET_TICKS);
      s_steps = APP_DEFAULT_TARGET_TICKS;
      (void)config->set_item("steps", s_steps);
    }
  }
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    // оставляем дефолт и запишем в NVS для прозрачности
    (void)config->set_item("steps", s_steps);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading steps: %s", esp_err_to_name(err));
  }

  // encoder (смещение)
  err = config->get_item("encoder", tmp);
  if (err == ESP_OK) s_encoder = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item("encoder", s_encoder);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading encoder: %s", esp_err_to_name(err));
  }

  // valve_off1..valve_offN (индивидуальные сдвиги)
  for (int i = 0; i < NUM_VALVES; i++) {
    char key[20];
    snprintf(key, sizeof(key), "valve_off%d", i + 1);
    err = config->get_item(key, tmp);
    if (err == ESP_OK) {
      s_valve_offset[i] = tmp;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
      s_valve_offset[i] = 0;
      (void)config->set_item(key, s_valve_offset[i]);
    } else {
      ESP_LOGW(CONFIG_TAG, "Error reading %s: %s", key, esp_err_to_name(err));
    }
  }

  // flush_valve_ms
  err = config->get_item("flush_valve_ms", tmp);
  if (err == ESP_OK) s_flush_valve_ms = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item("flush_valve_ms", s_flush_valve_ms);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading flush_valve_ms: %s", esp_err_to_name(err));
  }

  // flush_all_ms
  err = config->get_item("flush_all_ms", tmp);
  if (err == ESP_OK) s_flush_all_ms = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item("flush_all_ms", s_flush_all_ms);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading flush_all_ms: %s", esp_err_to_name(err));
  }

  // dry_run_timeout_ms
  err = config->get_item(NVS_KEY_DRY_RUN_TIMEOUT_MS, tmp);
  if (err == ESP_OK) s_dry_run_timeout_ms = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item(NVS_KEY_DRY_RUN_TIMEOUT_MS, s_dry_run_timeout_ms);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading dry_run_timeout_ms: %s", esp_err_to_name(err));
  }

  // dry_run_min_ticks
  err = config->get_item(NVS_KEY_DRY_RUN_MIN_TICKS, tmp);
  if (err == ESP_OK) s_dry_run_min_ticks = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item(NVS_KEY_DRY_RUN_MIN_TICKS, s_dry_run_min_ticks);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading dry_run_min_ticks: %s", esp_err_to_name(err));
  }

  // tick_source
  err = config->get_item("tick_source", tmp);
  if (err == ESP_OK) s_tick_source = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item("tick_source", s_tick_source);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading tick_source: %s", esp_err_to_name(err));
  }

  // tick_min_interval_us
  err = config->get_item(NVS_KEY_TICK_MIN_INTERVAL_US, tmp);
  if (err == ESP_OK) s_tick_min_interval_us = tmp;
  else if (err == ESP_ERR_NVS_NOT_FOUND) {
    (void)config->set_item(NVS_KEY_TICK_MIN_INTERVAL_US, s_tick_min_interval_us);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading tick_min_interval_us: %s", esp_err_to_name(err));
  }

  // tick_pull (0=OFF, 1=UP, 2=DOWN)
  err = config->get_item("tick_pull", tmp);
  if (err == ESP_OK) {
    s_tick_pull = tmp;
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Совместимость со старым ключом tick_pullup (0/1)
    int32_t legacy = 0;
    esp_err_t e2 = config->get_item("tick_pullup", legacy);
    if (e2 == ESP_OK) {
      s_tick_pull = (legacy != 0) ? 1 : 0;
    }
    (void)config->set_item("tick_pull", s_tick_pull);
  } else {
    ESP_LOGW(CONFIG_TAG, "Error reading tick_pull: %s", esp_err_to_name(err));
  }

  // Санитизация на всякий случай
  if (s_tick_pull < 0 || s_tick_pull > 2) s_tick_pull = 1;

  // commit на случай, если какие-то ключи отсутствовали и мы их проставили дефолтами
  (void)config->commit();

  if (steps) *steps = s_steps;
  if (encoder) *encoder = s_encoder;
  if (flush_valve_ms) *flush_valve_ms = s_flush_valve_ms;
  if (flush_all_ms) *flush_all_ms = s_flush_all_ms;
  return ESP_OK;
}

esp_err_t config_save_pump_settings(int32_t steps, int32_t encoder, int32_t flush_valve_ms, int32_t flush_all_ms) {
  if (!config) return ESP_ERR_INVALID_STATE;

  s_steps = steps;
  s_encoder = encoder;
  s_flush_valve_ms = flush_valve_ms;
  s_flush_all_ms = flush_all_ms;

  esp_err_t err = config->set_item("steps", s_steps);
  if (err != ESP_OK) return err;
  err = config->set_item("encoder", s_encoder);
  if (err != ESP_OK) return err;
  err = config->set_item("flush_valve_ms", s_flush_valve_ms);
  if (err != ESP_OK) return err;
  err = config->set_item("flush_all_ms", s_flush_all_ms);
  if (err != ESP_OK) return err;

  return config->commit();
}

esp_err_t config_init() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // CHECK_LOGE(nvs_flash_erase(), "Could not erase NVS flash");
    // CHECK_LOGE(nvs_flash_init(), "Could not init NVS flash");
    nvs_flash_erase();
    nvs_flash_init();
    err = nvs_flash_init();
    config_reset = true;
  }
  ESP_ERROR_CHECK(err);

  // Open
  ESP_LOGI(CONFIG_TAG, "Opening Non-Volatile err (NVS) handle...");
  esp_err_t result;
  // Config Handle.
  config = nvs::open_nvs_handle("err", NVS_READWRITE, &result);
  if (err != ESP_OK) {
    ESP_LOGI(CONFIG_TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
  } else {
    ESP_LOGI(CONFIG_TAG, "Done");
    // Read
    ESP_LOGI(CONFIG_TAG, "Reading restart counter from NVS ...");
    // value will default to 0, if not set yet in NVS
    err = config->get_item("restart_counter", restart_counter);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Restart counter: %d", restart_counter);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "The value is not initialized yet!");
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    // Загружаем общий счётчик банок
    ESP_LOGI(CONFIG_TAG, "Reading total banks counter from NVS ...");
    err = config->get_item("total_banks", app_state.total_banks_count);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Total banks counter: %ld", app_state.total_banks_count);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "Total banks counter is not initialized yet!");
      app_state.total_banks_count = 0;
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading total banks counter!", esp_err_to_name(err));
      app_state.total_banks_count = 0;
    }

    // Загружаем дневной счётчик банок
    ESP_LOGI(CONFIG_TAG, "Reading today banks counter from NVS ...");
    err = config->get_item("today_banks", app_state.today_banks_count);
    switch (err) {
    case ESP_OK:
      ESP_LOGW(CONFIG_TAG, "Today banks counter: %ld", app_state.today_banks_count);
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(CONFIG_TAG, "Today banks counter is not initialized yet!");
      app_state.today_banks_count = 0;
      break;
    default:
      ESP_LOGI(CONFIG_TAG, "Error (%s) reading today banks counter!", esp_err_to_name(err));
      app_state.today_banks_count = 0;
    }

    // Проверяем и сбрасываем дневной счётчик если нужно
    check_and_reset_daily_counter();

    // Загружаем уставки (steps/encoder/flush тайминги) в кэш
    {
      int32_t steps = 0, enc = 0, f1 = 0, f2 = 0;
      (void)config_load_pump_settings(&steps, &enc, &f1, &f2);
      ESP_LOGW(CONFIG_TAG, "Pump settings: steps=%ld encoder=%ld flush_valve_ms=%ld flush_all_ms=%ld dry_run_timeout_ms=%ld dry_run_min_ticks=%ld tick_source=%ld tick_min_interval_us=%ld",
               (long)steps, (long)enc, (long)f1, (long)f2,
               (long)s_dry_run_timeout_ms, (long)s_dry_run_min_ticks,
               (long)s_tick_source, (long)s_tick_min_interval_us);
    }

    err = config->get_string(NVS_KEY_DEVICE_NAME, s_device_name, sizeof(s_device_name));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      s_device_name[0] = '\0';
      (void)config->set_string(NVS_KEY_DEVICE_NAME, s_device_name);
    }

    err = config->get_string(NVS_KEY_TELEMETRY_URL, s_telemetry_url, sizeof(s_telemetry_url));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      s_telemetry_url[0] = '\0';
      (void)config->set_string(NVS_KEY_TELEMETRY_URL, s_telemetry_url);
    }
    ESP_LOGW(CONFIG_TAG, "Telemetry settings: device_name='%s' telemetry_url='%s'",
             s_device_name, s_telemetry_url);

    // Write
    ESP_LOGI(CONFIG_TAG, "Updating restart counter in NVS ... ");
    restart_counter++;
    err = config->set_item("restart_counter", restart_counter);
    if ((err != ESP_OK)) {
      ESP_LOGE(CONFIG_TAG, "Failed!");
    }

    // Commit written value.
    ESP_LOGI(CONFIG_TAG, "Committing updates in NVS ... ");
    err = config->commit();
    if ((err != ESP_OK)) {
      ESP_LOGE(CONFIG_TAG, "Failed!");
    }
  }
  return result;
}

esp_err_t save_total_banks_count(int32_t total_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(CONFIG_TAG, "Saving total banks counter to NVS: %ld", total_banks);
  esp_err_t err = config->set_item("total_banks", total_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to save total banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  // Commit written value
  err = config->commit();
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to commit total banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Total banks counter saved successfully");
  return ESP_OK;
}

esp_err_t save_today_banks_count(int32_t today_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(CONFIG_TAG, "Saving today banks counter to NVS: %ld", today_banks);
  esp_err_t err = config->set_item("today_banks", today_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to save today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  // Commit written value
  err = config->commit();
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to commit today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Today banks counter saved successfully");
  return ESP_OK;
}

esp_err_t load_today_banks_count(int32_t* today_banks) {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  esp_err_t err = config->get_item("today_banks", *today_banks);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to load today banks counter: %s", esp_err_to_name(err));
    return err;
  }
  
  ESP_LOGI(CONFIG_TAG, "Today banks counter loaded: %ld", *today_banks);
  return ESP_OK;
}

esp_err_t check_and_reset_daily_counter() {
  if (!config) {
    ESP_LOGE(CONFIG_TAG, "Config handle is not initialized!");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Получаем текущее время
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  if (!is_time_valid(timeinfo)) {
    ESP_LOGW(CONFIG_TAG,
             "Skipping daily counter check because current time is not valid yet: year=%d yday=%d",
             timeinfo.tm_year + 1900, timeinfo.tm_yday);
    return ESP_ERR_INVALID_STATE;
  }

  const int32_t current_day_id = make_day_id(timeinfo);
  
  // Загружаем дату последнего сброса. Новый формат хранит год и день года,
  // чтобы переход через 1 января тоже обрабатывался корректно.
  int32_t last_reset_id = 0;
  esp_err_t err = config->get_item(NVS_KEY_LAST_RESET_ID, last_reset_id);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    int32_t legacy_reset_day = 0;
    const esp_err_t legacy_err = config->get_item(NVS_KEY_LAST_RESET_DAY_LEGACY, legacy_reset_day);
    if (legacy_err == ESP_OK) {
      last_reset_id = (timeinfo.tm_year + 1900) * 1000 + legacy_reset_day;
      ESP_LOGI(CONFIG_TAG, "Migrating legacy last reset day=%ld to day_id=%ld",
               (long)legacy_reset_day, (long)last_reset_id);
    } else {
      last_reset_id = current_day_id;
      config->set_item(NVS_KEY_LAST_RESET_ID, last_reset_id);
      config->commit();
      ESP_LOGI(CONFIG_TAG, "First run - setting last reset day id to %ld", (long)last_reset_id);
      return ESP_OK;
    }
  } else if (err != ESP_OK) {
    ESP_LOGE(CONFIG_TAG, "Failed to read last reset day id: %s", esp_err_to_name(err));
    return err;
  }

  if (current_day_id < last_reset_id) {
    ESP_LOGW(CONFIG_TAG,
             "Current day id %ld is older than last reset id %ld, skipping reset until time stabilizes",
             (long)current_day_id, (long)last_reset_id);
    return ESP_OK;
  }
  
  // Проверяем, нужно ли сбросить счётчик (новый день)
  if (current_day_id != last_reset_id) {
    ESP_LOGI(CONFIG_TAG,
             "New day detected! Resetting daily counter. Old day id: %ld, New day id: %ld",
             (long)last_reset_id, (long)current_day_id);
    
    // Сбрасываем дневной счётчик
    app_state.today_banks_count = 0;
    save_today_banks_count(0);
    
    // Обновляем дату последнего сброса
    config->set_item(NVS_KEY_LAST_RESET_ID, current_day_id);
    config->set_item(NVS_KEY_LAST_RESET_DAY_LEGACY, timeinfo.tm_yday);
    config->commit();
    
    ESP_LOGI(CONFIG_TAG, "Daily counter reset successfully");
  } else {
    ESP_LOGI(CONFIG_TAG, "Same day - no reset needed. Current day id: %ld", (long)current_day_id);
  }
  
  return ESP_OK;
}
