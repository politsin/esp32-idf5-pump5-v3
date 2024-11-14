#pragma once

#include "initSequence.h"
#include <sdkconfig.h>

#define BOARD_NONE_PIN (-1)

#define BOARD_POWERON (gpio_num_t)(14)

#define BOARD_SPI_MISO (21)
#define BOARD_SPI_MOSI (19)
#define BOARD_SPI_SCK (18)
#define BOARD_TFT_CS (5)
#define BOARD_TFT_RST (23)
#define BOARD_TFT_DC (16)
#define BOARD_TFT_BL (4)

#define AMOLED_WIDTH (135)
#define AMOLED_HEIGHT (240)

#define BOARD_HAS_TOUCH 0

#define DISPLAY_BUFFER_SIZE (AMOLED_WIDTH * 100)

#define DISPLAY_FULLRESH false
