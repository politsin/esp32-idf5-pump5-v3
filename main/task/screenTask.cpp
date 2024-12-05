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
#include "buttonTask.h"

#include "util/config.h"

#include "esp_spiffs.h"
#include "fontx.h"
#include "st7789.h"
#include <benchmark/lv_demo_benchmark.h>
#include <esp_timer.h>
#include <hal/lv_hal_disp.h>
#include <main.h>
#include <screen/power_driver.h>
#include <screen/product_pins.h>
#include <screen/tft_driver.h>
#include <string.h>
#include <sys/dirent.h>
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

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 200
bool app_lvgl_lock(int timeout_ms);
void app_lvgl_unlock(void);
static void app_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                              lv_color_t *color_map);
static void app_increase_lvgl_tick(void *arg);

TaskHandle_t screen;
void screenTask(void *pvParam) {

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
  disp_drv.flush_cb = app_lvgl_flush_cb;
  disp_drv.draw_buf = &disp_buf;
  disp_drv.full_refresh = DISPLAY_FULLRESH;
  lv_disp_drv_register(&disp_drv);

  ESP_LOGI(SCREEN_TAG, "Install LVGL tick timer");
  // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
  const esp_timer_create_args_t lvgl_tick_timer_args = {
      .callback = &app_increase_lvgl_tick,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false};
  esp_timer_handle_t lvgl_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);

  lvgl_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_mux);

  ESP_LOGI(SCREEN_TAG, "Display LVGL");
  if (app_lvgl_lock(-1)) {
    // lv_demo_widgets();
    // lv_demo_benchmark();
    // lv_demo_stress();
    // lv_demo_music();
    // Release the mutex
    app_lvgl_unlock();
  }
  // Change the active screen's background color

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x003a57), LV_PART_MAIN);

  // Create a white label, set its text and align it to the center*/
  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "water");
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);

  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

  int btnX = 20; // Margin from right edge
  // Distance from the top
  int btnY_red = 10;
  int btnY_yell = 50;
  int btnY_blue = 90;

  // Function to create a rectangle with specified color and position
  auto createFig = [&](lv_color_t color, int y) -> lv_obj_t * {
    lv_obj_t *fig = lv_obj_create(lv_scr_act());
    lv_obj_set_size(fig, 30, 30);
    lv_obj_set_style_bg_color(fig, color, LV_PART_MAIN);
    lv_obj_set_style_border_color(fig, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(fig, 3, LV_PART_MAIN);
    lv_obj_align(fig, LV_ALIGN_TOP_RIGHT, -btnX, y);
    return fig;
  };

  // Create btn figure.
  lv_obj_t *btnRed = createFig(lv_color_hex(0xFF0000), btnY_red);
  lv_obj_t *btnYell = createFig(lv_color_hex(0xAAFF00), btnY_yell);
  lv_obj_t *btnBlue = createFig(lv_color_hex(0x0000FF), btnY_blue);

  uint32_t notification = 0;
  TickType_t lastUpdate = xTaskGetTickCount();

  while (true) {

    if (app_state.is_on) {
      if (xTaskNotifyWait(0x0, ULONG_MAX, &notification, portMAX_DELAY) ==
          pdTRUE) { 

        char labelText[128];

        snprintf(labelText, sizeof(labelText),
                 "Encoder: %ld\nTarget: %ld\nCurrent: %ld\n>> Delta: "
                 "%ld\nTime: %ld sec\n",
                 app_state.encoder, app_state.water_target,
                 app_state.water_current, app_state.water_delta,
                 app_state.time);

        if (notification & YELL_BUTTON_CLICKED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Yellow button clicked");
          lv_obj_set_style_border_color(btnYell, lv_color_hex(0xFF0000),
                                        LV_PART_MAIN);
          strcat(labelText, "Yellow Clicked\n");
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (notification & RED_BUTTON_PRESSED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Red button pressed");
          strcat(labelText, "Red Pressed\n");
          lv_obj_set_style_border_color(btnRed, lv_color_hex(0xFF0000),
                                        LV_PART_MAIN);
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (notification & RED_BUTTON_RELEASED_BIT) {
          lv_obj_set_style_border_color(btnYell, lv_color_white(),
                                        LV_PART_MAIN);
        }
        if (notification & YELL_BUTTON_RELEASED_BIT) {
          lv_obj_set_style_border_color(btnRed, lv_color_white(), LV_PART_MAIN);
        }

        if (notification & COUNTER_FINISHED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Counter Finished");
          strcat(labelText, "Counter Finished\n");
        }

        if (app_lvgl_lock(LVGL_TASK_MAX_DELAY_MS)) {
          lv_label_set_text(label, labelText);
          app_lvgl_unlock();
        }

        // vTaskDelay(pdMS_TO_TICKS(1000));
        if (app_lvgl_lock(LVGL_TASK_MAX_DELAY_MS)) {
          // Optionally clear the events after a period (keep app_state)
          //  lv_label_set_text(label, "");
          app_lvgl_unlock();
        }
      }
    } else {
      if (xTaskGetTickCount() - lastUpdate >=
          pdMS_TO_TICKS(1000)) {          // Check if 1 second has passed
        lastUpdate = xTaskGetTickCount(); // Update the last update time

        // Update app_state with current water level or whatever you need to
        // display periodically.

        // Prepare and display the label text (as before)...
        char labelText[128];
        snprintf(labelText, sizeof(labelText),
                 "Encoder: %ld\nTarget: %ld\nCurrent: %ld\n>> Delta: "
                 "%ld\nTime: %ld sec\n",
                 app_state.encoder, app_state.water_target,
                 app_state.water_current, app_state.water_delta,
                 app_state.time);

        if (app_lvgl_lock(LVGL_TASK_MAX_DELAY_MS)) {
          lv_label_set_text(label, labelText);
          app_lvgl_unlock();
        }

      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    lv_timer_handler(); // Handle LVGL updates
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void app_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                              lv_color_t *color_map) {
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  display_push_colors(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1,
                      (uint16_t *)color_map);
}

static void app_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool app_lvgl_lock(int timeout_ms) {
  // Convert timeout in milliseconds to FreeRTOS ticks
  // If `timeout_ms` is set to -1, the program will block until the condition is
  // met
  const TickType_t timeout_ticks =
      (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void app_lvgl_unlock(void) { xSemaphoreGiveRecursive(lvgl_mux); }
