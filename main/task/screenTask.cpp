// #include <screenTask.h> WTF o.O
// Screen #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
static constexpr Pintype LED = GPIO_NUM_22;
#include "sdkconfig.h"
#include <esp_log.h>
#include <rom/gpio.h>
#define SCREEN_TAG "SCREEN"

#define FS_TAG "SPIFFS"
void initSPIFFS();
void useFonts();

#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define TFT_OFFSETX 52
#define TFT_OFFSETY 40

// #if 0
#define TFT_MOSI_GPIO 23
#define TFT_SCLK_GPIO 18
#define TFT_CS_GPIO -1
// #define TFT_DC_GPIO 27
#define TFT_DC_GPIO 25
#define TFT_RESET_GPIO 33
#define TFT_BL_GPIO 32
// #endif

#include "util/config.h"

#include "esp_spiffs.h"
#include "fontx.h"
#include "hx711Task.h"
#include "st7789.h"
#include <main.h>
#include <string.h>
#include <sys/dirent.h>
#define INTERVAL 400
#define WAIT vTaskDelay(INTERVAL)

TickType_t FillTest(TFT_t *dev, int width, int height);
TickType_t ColorBarTest(TFT_t *dev, int width, int height);
TickType_t ArrowTest(TFT_t *dev, FontxFile *fx, int width, int height);
void printText(TFT_t *dev, FontxFile *fx, char *text);
void printSteps(TFT_t *dev, FontxFile *fx);

TaskHandle_t screen;
void screenTask(void *pvParam) {
  const TickType_t xBlockTime = pdMS_TO_TICKS(2000);

  initSPIFFS();

  TFT_t dev;
  spi_master_init(&dev, TFT_MOSI_GPIO, TFT_SCLK_GPIO, TFT_CS_GPIO, TFT_DC_GPIO,
                  TFT_RESET_GPIO, TFT_BL_GPIO);
  lcdInit(&dev, TFT_WIDTH, TFT_HEIGHT, TFT_OFFSETX, TFT_OFFSETY);

  // FillTest(&dev, TFT_WIDTH, TFT_HEIGHT);
  // ColorBarTest(&dev, TFT_WIDTH, TFT_HEIGHT);
  vTaskDelay(50);
  lcdFillScreen(&dev, BLACK);
  // xTaskNotify(hx711, 0, eSetValueWithOverwrite);
  // Fonts:
  FontxFile fx24G[2];
  FontxFile fx32G[2];
  InitFontx(fx24G, "/spiffs/ILGH24XB.FNT", ""); // 12x24Dot Gothic
  InitFontx(fx32G, "/spiffs/ILGH32XB.FNT", ""); // 16x32Dot Gothic
  // Scales:
  float weight;
  int32_t val = 0;
  uint32_t notify_value;
  // char value[16];

  while (true) {
    if (true) {
      ESP_LOGW(SCREEN_TAG, "screen!");
      if (xTaskNotifyWait(0, 0, &notify_value, 0) == pdTRUE) {
        val = (int32_t)notify_value;
        // weight = (float)scale / 1000;
        char value[16];
        sprintf(value, "enc: %ld", val);
        // sprintf(data, "%0.3f", val);
        // ESP_LOGI(SCREEN_TAG, "w: %0.2f", weight);
        // printText(&dev, fx32G, value);
        printSteps(&dev, fx32G);
      }
    }
    vTaskDelay(xBlockTime);
  }
}

void printSteps(TFT_t *dev, FontxFile *fx) {
  char speed[16];
  char steps[16];
  sprintf(speed, "encoder: %ld", app_config.encoder);
  sprintf(steps, "steps: %ld", app_config.steps);

  int width = CONFIG_WIDTH;
  int height = CONFIG_HEIGHT;
  // get font width & height
  uint8_t buffer[FontxGlyphBufSize];
  uint8_t fontWidth;
  uint8_t fontHeight;
  GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
  lcdFillScreen(dev, BLACK);
  lcdSetFontDirection(dev, DIRECTION90);

  uint16_t xpos;
  uint16_t ypos;
  uint16_t color = WHITE;
  uint8_t ascii[24];
  strcpy((char *)ascii, speed);
  xpos = ((width - fontHeight) / 2) - 1;
  ypos = (height - (strlen((char *)ascii) * fontWidth)) / 2;
  lcdDrawString(dev, fx, xpos - 30, ypos, ascii, color);
  strcpy((char *)ascii, steps);
  lcdDrawString(dev, fx, xpos - 60, ypos, ascii, color);
}

void printText(TFT_t *dev, FontxFile *fx, char *text) {
  int width = CONFIG_WIDTH;
  int height = CONFIG_HEIGHT;
  // get font width & height
  uint8_t buffer[FontxGlyphBufSize];
  uint8_t fontWidth;
  uint8_t fontHeight;
  GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
  // ESP_LOGI(__FUNCTION__,"fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

  uint16_t xpos;
  uint16_t ypos;
  uint8_t ascii[24];
  uint16_t color;

  lcdFillScreen(dev, BLACK);

  strcpy((char *)ascii, text);
  xpos = ((width - fontHeight) / 2) - 1;
  ypos = (height - (strlen((char *)ascii) * fontWidth)) / 2;
  lcdSetFontDirection(dev, DIRECTION90);
  color = WHITE;
  lcdDrawString(dev, fx, xpos, ypos, ascii, color);
}

void useFonts() {
  FontxFile fx16G[2];
  FontxFile fx24G[2];
  FontxFile fx32G[2];
  FontxFile fx32L[2];
  InitFontx(fx16G, "/spiffs/ILGH16XB.FNT", ""); // 8x16Dot Gothic
  InitFontx(fx24G, "/spiffs/ILGH24XB.FNT", ""); // 12x24Dot Gothic
  InitFontx(fx32G, "/spiffs/ILGH32XB.FNT", ""); // 16x32Dot Gothic
  InitFontx(fx32L, "/spiffs/LATIN32B.FNT", ""); // 16x32Dot Latin

  FontxFile fx16M[2];
  FontxFile fx24M[2];
  FontxFile fx32M[2];
  InitFontx(fx16M, "/spiffs/ILMH16XB.FNT", ""); // 8x16Dot Mincyo
  InitFontx(fx24M, "/spiffs/ILMH24XB.FNT", ""); // 12x24Dot Mincyo
  InitFontx(fx32M, "/spiffs/ILMH32XB.FNT", ""); // 16x32Dot Mincyo
};

static void SPIFFS_Directory(const char *path) {
  DIR *dir = opendir(path);
  assert(dir != NULL);
  while (true) {
    struct dirent *pe = readdir(dir);
    if (!pe)
      break;
    ESP_LOGI(__FUNCTION__, "d_name=%s d_ino=%d d_type=%x", pe->d_name,
             pe->d_ino, pe->d_type);
  }
  closedir(dir);
}

void initSPIFFS() {
  ESP_LOGI(FS_TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = NULL,
                                .max_files = 10,
                                .format_if_mount_failed = true};

  // Use settings defined above toinitialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is anall-in-one convenience function.
  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(FS_TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(FS_TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(FS_TAG, "Failed to initialize SPIFFS (%s)",
               esp_err_to_name(ret));
    }
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(FS_TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(FS_TAG, "Partition size: total: %d, used: %d", total, used);
  }

  SPIFFS_Directory("/spiffs/");
}

TickType_t FillTest(TFT_t *dev, int width, int height) {
  TickType_t startTick, endTick, diffTick;
  startTick = xTaskGetTickCount();

  lcdFillScreen(dev, RED);
  vTaskDelay(50);
  lcdFillScreen(dev, GREEN);
  vTaskDelay(50);
  lcdFillScreen(dev, BLUE);
  vTaskDelay(50);

  endTick = xTaskGetTickCount();
  diffTick = endTick - startTick;
  ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%ld", diffTick * portTICK_PERIOD_MS);
  return diffTick;
}

TickType_t ColorBarTest(TFT_t *dev, int width, int height) {
  TickType_t startTick, endTick, diffTick;
  startTick = xTaskGetTickCount();

  uint16_t x1, x2;
  x1 = width / 3;
  x2 = (width / 3) * 2;
  lcdDrawFillRect(dev, 0, 0, x1 - 1, height - 1, RED);
  vTaskDelay(1);
  lcdDrawFillRect(dev, x1, 0, x2 - 1, height - 1, BLUE);
  vTaskDelay(1);
  lcdDrawFillRect(dev, x2, 0, width - 1, height - 1, WHITE);

  endTick = xTaskGetTickCount();
  diffTick = endTick - startTick;
  ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%ld", diffTick * portTICK_PERIOD_MS);
  return diffTick;
}

TickType_t ArrowTest(TFT_t *dev, FontxFile *fx, int width, int height) {
  TickType_t startTick, endTick, diffTick;
  startTick = xTaskGetTickCount();

  // get font width & height
  uint8_t buffer[FontxGlyphBufSize];
  uint8_t fontWidth;
  uint8_t fontHeight;
  GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
  // ESP_LOGI(__FUNCTION__,"fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

  uint16_t xpos;
  uint16_t ypos;
  int stlen;
  uint8_t ascii[24];
  uint16_t color;

  lcdFillScreen(dev, BLACK);

  strcpy((char *)ascii, "ST7789");
  if (width < height) {
    xpos = ((width - fontHeight) / 2) - 1;
    ypos = (height - (strlen((char *)ascii) * fontWidth)) / 2;
    lcdSetFontDirection(dev, DIRECTION90);
  } else {
    ypos = ((height - fontHeight) / 2) - 1;
    xpos = (width - (strlen((char *)ascii) * fontWidth)) / 2;
    lcdSetFontDirection(dev, DIRECTION0);
  }
  color = WHITE;
  lcdDrawString(dev, fx, xpos, ypos, ascii, color);

  lcdSetFontDirection(dev, 0);
  color = RED;
  lcdDrawFillArrow(dev, 10, 10, 0, 0, 5, color);
  strcpy((char *)ascii, "0,0");
  lcdDrawString(dev, fx, 0, 30, ascii, color);

  color = GREEN;
  lcdDrawFillArrow(dev, width - 11, 10, width - 1, 0, 5, color);
  // strcpy((char *)ascii, "79,0");
  sprintf((char *)ascii, "%d,0", width - 1);
  stlen = strlen((char *)ascii);
  xpos = (width - 1) - (fontWidth * stlen);
  lcdDrawString(dev, fx, xpos, 30, ascii, color);

  color = GRAY;
  lcdDrawFillArrow(dev, 10, height - 11, 0, height - 1, 5, color);
  // strcpy((char *)ascii, "0,159");
  sprintf((char *)ascii, "0,%d", height - 1);
  ypos = (height - 11) - (fontHeight) + 5;
  lcdDrawString(dev, fx, 0, ypos, ascii, color);

  color = CYAN;
  lcdDrawFillArrow(dev, width - 11, height - 11, width - 1, height - 1, 5,
                   color);
  // strcpy((char *)ascii, "79,159");
  sprintf((char *)ascii, "%d,%d", width - 1, height - 1);
  stlen = strlen((char *)ascii);
  xpos = (width - 1) - (fontWidth * stlen);
  lcdDrawString(dev, fx, xpos, ypos, ascii, color);

  endTick = xTaskGetTickCount();
  diffTick = endTick - startTick;
  ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%ld", diffTick * portTICK_PERIOD_MS);
  return diffTick;
}
