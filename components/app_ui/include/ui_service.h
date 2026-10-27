#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_service_init(void);

esp_err_t ui_service_apply_scene(int page, const char *scene_json, char *err, size_t err_len);
esp_err_t ui_service_clear(int page);
esp_err_t ui_service_set_page(int page);
esp_err_t ui_service_next_page(void);
esp_err_t ui_service_notify(const char *text, const char *level, int duration_ms);
esp_err_t ui_service_set_brightness(int percent);
esp_err_t ui_service_set_backlight(bool on);

int ui_service_current_page(void);
int ui_service_page_count(void);
const char *ui_service_current_scene(void);
const char *ui_service_get_scene(int page);
void ui_service_status_json(cJSON *root);

#ifdef __cplusplus
}
#endif
