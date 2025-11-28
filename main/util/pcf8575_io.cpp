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
// Клапаны: P0..P4
static constexpr int VALVE1_BIT = 0; // P0
static constexpr int VALVE2_BIT = 1; // P1
static constexpr int VALVE3_BIT = 2; // P2
static constexpr int VALVE4_BIT = 3; // P3
static constexpr int VALVE5_BIT = 4; // P4
// Кнопки: P8..P10 (активный низ)
static constexpr int BTN_STOP_BIT  = 8;  // P8
static constexpr int BTN_FLUSH_BIT = 9;  // P9
static constexpr int BTN_RUN_BIT   = 10; // P10

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

    ESP_ERROR_CHECK(pcf8575_init_desc(&pcf_dev, PCF8575_ADDR, I2C_NUM_0, sda, scl));

    // Начальное состояние: все '1' (входы/высокий уровень)
    port_shadow = 0xFFFF;
    // Сразу выключим все клапаны (P0..P4 = 0) без вызова ioexp_* (ещё не initialized)
    {
        const uint16_t valves_mask =
            (1u << VALVE1_BIT) |
            (1u << VALVE2_BIT) |
            (1u << VALVE3_BIT) |
            (1u << VALVE4_BIT) |
            (1u << VALVE5_BIT);
        port_shadow &= (uint16_t)~valves_mask;
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
        case 5: bit = VALVE5_BIT; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    return write_bit_and_flush(bit, level);
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
        (1u << VALVE4_BIT) |
        (1u << VALVE5_BIT);
    if (level)
        port_shadow |= mask;
    else
        port_shadow &= (uint16_t)~mask;
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


