#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t telemetry_init(void);
esp_err_t telemetry_send_fill_event(int32_t banks_run,
                                    int32_t banks_today,
                                    int32_t banks_total,
                                    int32_t valve_number);
const char *telemetry_resolved_device_id(void);

#ifdef __cplusplus
}
#endif

#endif
