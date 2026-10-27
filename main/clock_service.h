#pragma once

#include <stdbool.h>
#include <time.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t clock_service_init(void);
esp_err_t clock_service_start_sntp(void);

bool clock_service_time_valid(void);
esp_err_t clock_service_get_time(struct tm *out);
esp_err_t clock_service_set_time(const struct tm *in);

void clock_service_status_json(cJSON *root);

#ifdef __cplusplus
}
#endif
