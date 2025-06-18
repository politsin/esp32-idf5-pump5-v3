# Esp32 Pump

<https://github.com/GillesOdb/ttgo-tdisplay-kicad>

- 0 outputs PWM signal at boot, must be LOW to enter flashing mode
  - Input - pulled up
  - Output - OK
- 1 TX pin (Input TX, Output OK), debug output at boot
- 2 OK, must be floating or LOW during boot
- 3 RX pin (Input OK, Output RX), HIGH at boot
- 4 OK 
- 5 OK - must be HIGH during boot
- 12 OK - boot fails if pulled high, strapping pin
- 14 OK - outputs PWM signal at boot
- 15 OK - outputs PWM signal at boot, strapping pin
- 34, 35, 36, 39 - input only, no pull-up or pull-down

- 21 - i2c-DA
- 22 - i2c-CL
- 17 - ENC-2
- 2  - !! LOW
- 15 - ENC-2, boot PWM
- 13
- 12 - !! LOW

- 36 - SENSOR_VP - вход ацп
- 37 - BTN-1
- 38 - BTN-2
- 39 - BTN-ENC
- 32
- 33
- 25 - xEn ()
- 26 - xDa
- 27 - xCl

- 10 bat-ADC
- 

## OLED
```cpp
# define SCREEN_WIDTH 128 
# define SCREEN_HEIGHT 64

DAC1 (GPIO25)
DAC2 (GPIO26)
```

```cpp 
// button
static const Pintype BUTTON_PIN1 = GPIO_NUM_37;
static const Pintype BUTTON_PIN2 = GPIO_NUM_38;
// enc
static const gpio_num_t encoderS1 = GPIO_NUM_15;
static const gpio_num_t encoderS2 = GPIO_NUM_17;
static const gpio_num_t encoderBtn = GPIO_NUM_39;
// screen
#define TFT_BL_GPIO 4
#define TFT_CS_GPIO 5
#define TFT_DC_GPIO 16
#define TFT_SCLK_GPIO 18
#define TFT_MOSI_GPIO 19
#define TFT_RESET_GPIO 23
// i2c Arduino IDE
//#define I2C_SDA GPIO_NUM_21
// #define I2C_SCL GPIO_NUM_22
// General 4-PIN
static const gpio_num_t encoderS1 = GPIO_NUM_25;
static const gpio_num_t encoderS2 = GPIO_NUM_26;
static const gpio_num_t encoderBtn = GPIO_NUM_27;
```
