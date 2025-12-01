// #include <screenTask.h> WTF o.O
// Screen #22 #19.
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
typedef gpio_num_t Pintype;
// static constexpr Pintype LED = GPIO_NUM_22;
#include "sdkconfig.h"
#include <esp_log.h>
#include <rom/gpio.h>
#define SCREEN_TAG "SCREEN"

// #endif
#include "buttonTask.h"

#include "util/config.h"

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

  // Create основной label слева
  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "water");
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 8);

  // Создаём отдельные label для P1–P4 справа
  lv_obj_t *valve_labels[NUM_VALVES];
  lv_obj_t *valve_indicators[NUM_VALVES]; // Кружочки для индикации активных клапанов
  int valve_label_y_start = 8;
  int valve_label_y_step = 18;
  for (int i = 0; i < NUM_VALVES; i++) {
    // Создаём кружочек-индикатор
    valve_indicators[i] = lv_obj_create(lv_scr_act());
    lv_obj_set_size(valve_indicators[i], 8, 8);
    lv_obj_set_style_radius(valve_indicators[i], 4, LV_PART_MAIN); // Делаем круглым
    lv_obj_set_style_bg_color(valve_indicators[i], lv_color_hex(0x333333), LV_PART_MAIN); // Изначально тёмно-серый
    lv_obj_set_style_border_width(valve_indicators[i], 0, LV_PART_MAIN);
    lv_obj_align(valve_indicators[i], LV_ALIGN_TOP_RIGHT, -85, valve_label_y_start + i * valve_label_y_step + 4);
    
    // Создаём label для времени клапана (объединённый)
    valve_labels[i] = lv_label_create(lv_scr_act());
    char txt[16];
    snprintf(txt, sizeof(txt), "P%d: %.2f s", i+1, (double)app_state.valve_times[i] / 100.0);
    lv_label_set_text(valve_labels[i], txt);
    lv_obj_set_style_text_color(valve_labels[i], lv_color_hex(0x00ffcc), LV_PART_MAIN);
    lv_obj_align(valve_labels[i], LV_ALIGN_TOP_RIGHT, -8, valve_label_y_start + i * valve_label_y_step);
  }

  // Создаём label для общей суммы времени
  lv_obj_t *total_time_label = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(total_time_label, lv_color_hex(0xffff00), LV_PART_MAIN); // Жёлтый цвет для выделения
  lv_obj_align(total_time_label, LV_ALIGN_TOP_RIGHT, -8, valve_label_y_start + NUM_VALVES * valve_label_y_step + 10);
  
  // Инициализируем текст общей суммы
  uint32_t total_time = 0;
  for (int i = 0; i < NUM_VALVES; i++) {
    total_time += app_state.valve_times[i];
  }
  char total_txt[32];
  snprintf(total_txt, sizeof(total_txt), "Total: %.2f s", (double)total_time / 100.0);
  lv_label_set_text(total_time_label, total_txt);

  // Перемещаем кнопки вниз экрана в линию
  int btnY_bottom = 100; // Было 120, стало 100 (подняли ещё выше)
  int btnSpacing = 35;
  int btnX_start = 10;
  auto createFig = [&](lv_color_t color, int x) -> lv_obj_t * {
    lv_obj_t *fig = lv_obj_create(lv_scr_act());
    lv_obj_set_size(fig, 25, 25); // Было 30x30, стало 25x25 (уменьшили размер)
    lv_obj_set_style_bg_color(fig, color, LV_PART_MAIN);
    lv_obj_set_style_border_color(fig, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(fig, 3, LV_PART_MAIN);
    lv_obj_align(fig, LV_ALIGN_TOP_LEFT, x, btnY_bottom);
    return fig;
  };
  lv_obj_t *btnRed = createFig(lv_color_hex(0xFF0000), btnX_start);
  lv_obj_t *btnYell = createFig(lv_color_hex(0xFFFF00), btnX_start + btnSpacing);
  createFig(lv_color_hex(0x0000FF), btnX_start + btnSpacing * 2);

  uint32_t notification = 0;
  TickType_t lastUpdate = xTaskGetTickCount();

  while (true) {
    if (app_state.is_on) {
      if (xTaskNotifyWait(0x0, ULONG_MAX, &notification, portMAX_DELAY) == pdTRUE) {
        char labelText[192];
        int32_t total_seconds, minutes, seconds, banks_count;
        
        if (app_state.counter_error) {
          // Ошибка счётчика - показываем Err
          banks_count = app_state.final_banks;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: Err",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count);
        } else if (app_state.start_time > 0) {
          // Работает - показываем текущие значения
          total_seconds = (xTaskGetTickCount() - app_state.start_time) / 100;
          banks_count = app_state.banks_count;
          minutes = total_seconds / 60;
          seconds = total_seconds % 60;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: %02ld:%02ld",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count,
                   minutes, seconds);
        } else {
          // Остановлено - показываем финальные значения
          total_seconds = app_state.final_time;
          banks_count = app_state.final_banks;
          minutes = total_seconds / 60;
          seconds = total_seconds % 60;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: %02ld:%02ld",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count,
                   minutes, seconds);
        }
        if (notification & BTN1_BUTTON_CLICKED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Yellow button clicked");
          lv_obj_set_style_border_color(btnYell, lv_color_hex(0xFF0000), LV_PART_MAIN);
          strcat(labelText, "\nYellow Clicked");
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (notification & BTN2_BUTTON_CLICKED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Red button pressed");
          strcat(labelText, "\nRed Pressed");
          lv_obj_set_style_border_color(btnRed, lv_color_hex(0xFF0000), LV_PART_MAIN);
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (notification & BTN_STOP_BIT) {
          lv_obj_set_style_border_color(btnYell, lv_color_white(), LV_PART_MAIN);
        }
        if (notification & BTN_RUN_BIT) {
          lv_obj_set_style_border_color(btnRed, lv_color_white(), LV_PART_MAIN);
        }
        if (notification & COUNTER_FINISHED_BIT) {
          ESP_LOGI(SCREEN_TAG, "Counter Finished");
          strcat(labelText, "\nCounter Finished");
        }
        if (app_lvgl_lock(LVGL_TASK_MAX_DELAY_MS)) {
          lv_label_set_text(label, labelText);
          for (int i = 0; i < NUM_VALVES; i++) {
            char txt[16];
            snprintf(txt, sizeof(txt), "P%d: %.2f s", i+1, (double)app_state.valve_times[i] / 100.0);
            lv_label_set_text(valve_labels[i], txt);
            
            // Показываем зелёный кружочек, если клапан активен И помпа работает
            if (app_state.is_on && (i + 1 == app_state.valve || (app_state.valve == 0 && app_state.is_on))) {
              lv_obj_set_style_bg_color(valve_indicators[i], lv_color_hex(0x00FF00), LV_PART_MAIN); // Зелёный
            } else {
              lv_obj_set_style_bg_color(valve_indicators[i], lv_color_hex(0x333333), LV_PART_MAIN); // Тёмно-серый
            }
          }
          
          // Обновляем общую сумму времени
          uint32_t total_time = 0;
          for (int i = 0; i < NUM_VALVES; i++) {
            total_time += app_state.valve_times[i];
          }
          char total_txt[32];
          snprintf(total_txt, sizeof(total_txt), "Total: %.2f s", (double)total_time / 100.0);
          lv_label_set_text(total_time_label, total_txt);
          app_lvgl_unlock();
        }
      }
    } else {
      if (xTaskGetTickCount() - lastUpdate >= pdMS_TO_TICKS(1000)) {
        lastUpdate = xTaskGetTickCount();
        char labelText[192];
        int32_t total_seconds, minutes, seconds, banks_count;
        
        if (app_state.counter_error) {
          // Ошибка счётчика - показываем Err
          banks_count = app_state.final_banks;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: Err",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count);
        } else if (app_state.start_time > 0) {
          // Работает - показываем текущие значения
          total_seconds = (xTaskGetTickCount() - app_state.start_time) / 100;
          banks_count = app_state.banks_count;
          minutes = total_seconds / 60;
          seconds = total_seconds % 60;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: %02ld:%02ld",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count,
                   minutes, seconds);
        } else {
          // Остановлено - показываем финальные значения
          total_seconds = app_state.final_time;
          banks_count = app_state.final_banks;
          minutes = total_seconds / 60;
          seconds = total_seconds % 60;
          snprintf(labelText, sizeof(labelText),
                   "Encoder: %ld\nTarget: %ld>%ld\nCurrent: %ld\n>> Banks: %ld\nTime: %02ld:%02ld",
                   app_state.encoder, app_state.previous_target, app_state.water_target,
                   app_state.water_current, banks_count,
                   minutes, seconds);
        }
        if (app_lvgl_lock(LVGL_TASK_MAX_DELAY_MS)) {
          lv_label_set_text(label, labelText);
          for (int i = 0; i < NUM_VALVES; i++) {
            char txt[16];
            snprintf(txt, sizeof(txt), "P%d: %.2f s", i+1, (double)app_state.valve_times[i] / 100.0);
            lv_label_set_text(valve_labels[i], txt);
            
            // Показываем зелёный кружочек, если клапан активен И помпа работает
            if (app_state.is_on && (i + 1 == app_state.valve || (app_state.valve == 0 && app_state.is_on))) {
              lv_obj_set_style_bg_color(valve_indicators[i], lv_color_hex(0x00FF00), LV_PART_MAIN); // Зелёный
            } else {
              lv_obj_set_style_bg_color(valve_indicators[i], lv_color_hex(0x333333), LV_PART_MAIN); // Тёмно-серый
            }
          }
          
          // Обновляем общую сумму времени
          uint32_t total_time = 0;
          for (int i = 0; i < NUM_VALVES; i++) {
            total_time += app_state.valve_times[i];
          }
          char total_txt[32];
          snprintf(total_txt, sizeof(total_txt), "Total: %.2f s", (double)total_time / 100.0);
          lv_label_set_text(total_time_label, total_txt);
          app_lvgl_unlock();
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    lv_timer_handler();
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
