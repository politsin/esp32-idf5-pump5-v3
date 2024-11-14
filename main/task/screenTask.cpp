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

// #endif

#include "util/config.h"

#include "esp_spiffs.h"
#include "fontx.h"
#include "st7789.h"
#include <main.h>
#include <string.h>
#include <sys/dirent.h>
#include <hal/lv_hal_disp.h>
#include <screen/tft_driver.h>
#include <screen/power_driver.h>
#include <screen/product_pins.h>
#include <esp_timer.h>
#include <benchmark/lv_demo_benchmark.h>
// #include "hx711Task.h"

// #include <TFT_eSPI.h>
// #include <TFT_config.h>

// TFT Driver	ST7789
#define TFT_BL 4
#define TFT_CS 5
#define TFT_DC 16
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_RST 23

static SemaphoreHandle_t lvgl_mux = NULL;
// contains internal graphic buffer(s) called draw buffer(s)
static lv_disp_draw_buf_t disp_buf;
extern "C" {
  // contains callback functions
  lv_disp_drv_t disp_drv;
}

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void example_increase_lvgl_tick(void *arg);

TaskHandle_t screen;
void screenTask(void *pvParam) {
  const TickType_t xBlockTime = pdMS_TO_TICKS(2000);
  uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;

  power_driver_init();
  display_init();
  lv_init();

  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
      AMOLED_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf1);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
      AMOLED_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf2);
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, AMOLED_HEIGHT * 20);

  ESP_LOGI(SCREEN_TAG, "Register display driver to LVGL");
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = AMOLED_HEIGHT;
  disp_drv.ver_res = AMOLED_WIDTH;
  disp_drv.flush_cb = example_lvgl_flush_cb;
  disp_drv.draw_buf = &disp_buf;
  disp_drv.full_refresh = DISPLAY_FULLRESH;
  lv_disp_drv_register(&disp_drv);

  ESP_LOGI(SCREEN_TAG, "Install LVGL tick timer");
  // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &example_increase_lvgl_tick,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false};
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer,
                                           EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

  lvgl_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_mux);

  ESP_LOGI(SCREEN_TAG, "Display LVGL");
  if (example_lvgl_lock(-1)) {
    // lv_demo_widgets();
    lv_demo_benchmark();
    // lv_demo_stress();
    // lv_demo_music();
    // Release the mutex
    example_lvgl_unlock();
  }
  while (true) {
    if (example_lvgl_lock(-1)) {
      task_delay_ms = lv_timer_handler();
      // Release the mutex
      example_lvgl_unlock();
    }
    if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
      task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
      task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(xBlockTime);
  }
}


static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  display_push_colors(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1,
                      (uint16_t *)color_map);
}

static void example_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool example_lvgl_lock(int timeout_ms) {
  // Convert timeout in milliseconds to FreeRTOS ticks
  // If `timeout_ms` is set to -1, the program will block until the condition is
  // met
  const TickType_t timeout_ticks =
      (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void) { xSemaphoreGiveRecursive(lvgl_mux); }
