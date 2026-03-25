#ifndef TELEMETRY_TASK_H
#define TELEMETRY_TASK_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t telemetryTaskHandle;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t fill_seq;
  int32_t banks_run;
  int32_t banks_today;
  int32_t banks_total;
  int32_t valve_number;
  int32_t encoder_offset;
} telemetry_fill_event_t;

void telemetryTask(void *pvParameter);
esp_err_t telemetry_queue_init(void);
esp_err_t telemetry_send_fill_event_async(const telemetry_fill_event_t *event);

#ifdef __cplusplus
}
#endif

#endif
