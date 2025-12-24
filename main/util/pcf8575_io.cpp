#include "pcf8575_io.h"
#include <pcf8575.h>
#include <i2cdev.h>
#include <esp_log.h>
#include "../util/i2c.h"

static const char* TAG_IOEXP = "IOEXP";

// Адрес PCF8575: 0x20..0x27 в зависимости от A0..A2
#ifndef PCF8575_ADDR
#define PCF8575_ADDR (PCF8575_I2C_ADDR_BASE) // 0x20
#endif

// Соответствие битов портов PCF8575
// Нижний байт = P0..P7, верхний байт = P8..P15
// Клапаны: P10..P13
static constexpr int VALVE1_BIT = 10; // P10
static constexpr int VALVE2_BIT = 11; // P11
static constexpr int VALVE3_BIT = 12; // P12
static constexpr int VALVE4_BIT = 13; // P13
// Кнопки: P1..P3 (активный низ)
static constexpr int BTN_STOP_BIT  = 1;  // P1
static constexpr int BTN_FLUSH_BIT = 2;  // P2
static constexpr int BTN_RUN_BIT   = 3;  // P3
// Помпа: P15
static constexpr int PUMP_BIT = 15; // P15

static i2c_dev_t pcf_dev{};
static uint16_t port_shadow = 0xFFFF; // 1 = подтяжка/вход; 0 = выводим '0'
static bool initialized = false;

// Установить/сбросить бит в port_shadow и записать в микросхему
static esp_err_t write_bit_and_flush(int bit_index, bool high_level)
{
    uint16_t mask = (uint16_t)1u << bit_index;
    if (high_level)
        port_shadow |= mask;
    else
        port_shadow &= (uint16_t)~mask;
    return pcf8575_port_write(&pcf_dev, port_shadow);
}

esp_err_t ioexp_init(void)
{
    if (initialized) return ESP_OK;

    // I2C и i2cdev уже инициализируются в app_main()
    // Здесь только настраиваем дескриптор PCF8575

    // Пины SDA/SCL берём из util/i2c.h (I2C_SDA/I2C_SCL)
    const gpio_num_t sda = I2C_SDA;
    const gpio_num_t scl = I2C_SCL;

    // Важно: в app_main() для тега "IOEXP" выставлен уровень WARN, поэтому
    // печатаем порт/пины/адрес через ESP_LOGW, чтобы это было видно в мониторе.
    ESP_LOGW(TAG_IOEXP, "PCF8575 init: addr=0x%02X port=%d SDA=%d SCL=%d",
             (int)PCF8575_ADDR, (int)I2C_NUM_0, (int)sda, (int)scl);

    ESP_ERROR_CHECK(pcf8575_init_desc(&pcf_dev, PCF8575_ADDR, I2C_NUM_0, sda, scl));

    // Начальное состояние (активный низ для выходов):
    // - Кнопки (P1..P3) держим '1' для чтения
    // - Выходы (P10..P13 клапаны, P15 помпа) выключены '1' (OFF)
    {
        const uint16_t outputs_mask =
            (1u << VALVE1_BIT) |
            (1u << VALVE2_BIT) |
            (1u << VALVE3_BIT) |
            (1u << VALVE4_BIT) |
            (1u << PUMP_BIT);
        port_shadow = 0xFFFF;
        port_shadow |= outputs_mask; // выходы = 1 (OFF), входы остаются 1
        esp_err_t r = pcf8575_port_write(&pcf_dev, port_shadow);
        if (r != ESP_OK) {
            ESP_LOGE(TAG_IOEXP, "PCF8575 write failed at 0x%02X (check wiring/address). err=0x%x", PCF8575_ADDR, r);
            return r;
        }
    }

    initialized = true;
    ESP_LOGI(TAG_IOEXP, "PCF8575 initialized at 0x%02X", PCF8575_ADDR);
    return ESP_OK;
}

bool ioexp_is_initialized(void) { return initialized; }

esp_err_t ioexp_get_shadow(uint16_t *out_shadow) {
    if (!out_shadow) return ESP_ERR_INVALID_ARG;
    if (!initialized) return ESP_ERR_INVALID_STATE;
    *out_shadow = port_shadow;
    return ESP_OK;
}

static inline bool is_bit_on_active_low(uint16_t shadow, int bit_index) {
    // active-low: 0 = ON, 1 = OFF
    return (((shadow >> bit_index) & 0x1) == 0);
}

bool ioexp_get_pump(void) {
    if (!initialized) return false;
    return is_bit_on_active_low(port_shadow, PUMP_BIT);
}

bool ioexp_get_valve(int valve_index_1_based) {
    if (!initialized) return false;
    int bit = -1;
    switch (valve_index_1_based) {
        case 1: bit = VALVE1_BIT; break;
        case 2: bit = VALVE2_BIT; break;
        case 3: bit = VALVE3_BIT; break;
        case 4: bit = VALVE4_BIT; break;
        default: return false;
    }
    return is_bit_on_active_low(port_shadow, bit);
}

esp_err_t ioexp_toggle_pump(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    return ioexp_set_pump(!ioexp_get_pump());
}

esp_err_t ioexp_toggle_valve(int valve_index_1_based) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    return ioexp_set_valve(valve_index_1_based, !ioexp_get_valve(valve_index_1_based));
}

esp_err_t ioexp_set_pump(bool level)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Активный низ: true -> 0, false -> 1
    return write_bit_and_flush(PUMP_BIT, !level);
}

esp_err_t ioexp_set_valve(int valve_index_1_based, bool level)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    int bit = -1;
    switch (valve_index_1_based) {
        case 1: bit = VALVE1_BIT; break;
        case 2: bit = VALVE2_BIT; break;
        case 3: bit = VALVE3_BIT; break;
        case 4: bit = VALVE4_BIT; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    // Активный низ: true -> 0, false -> 1
    return write_bit_and_flush(bit, !level);
}

esp_err_t ioexp_set_all_valves(bool level)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t mask =
        (1u << VALVE1_BIT) |
        (1u << VALVE2_BIT) |
        (1u << VALVE3_BIT) |
        (1u << VALVE4_BIT);
    // Активный низ
    if (level) // включить все
        port_shadow &= (uint16_t)~mask; // 0 = ON
    else       // выключить все
        port_shadow |= mask;            // 1 = OFF
    return pcf8575_port_write(&pcf_dev, port_shadow);
}

esp_err_t ioexp_read_buttons(bool* stop_pressed, bool* flush_pressed, bool* run_pressed)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!stop_pressed || !flush_pressed || !run_pressed) return ESP_ERR_INVALID_ARG;

    // Для входов на PCF8575 бит должен быть '1', чтобы ножка была входом/подтянута вверх
    // Мы держим весь верхний байт в '1'. Просто читаем порт и трактуем 0 как нажатие.
    uint16_t val = 0xFFFF;
    ESP_ERROR_CHECK(pcf8575_port_read(&pcf_dev, &val));

    const bool stop_low  = ((val >> BTN_STOP_BIT)  & 0x1) == 0;
    const bool flush_low = ((val >> BTN_FLUSH_BIT) & 0x1) == 0;
    const bool run_low   = ((val >> BTN_RUN_BIT)   & 0x1) == 0;

    *stop_pressed  = stop_low;
    *flush_pressed = flush_low;
    *run_pressed   = run_low;
    return ESP_OK;
}


