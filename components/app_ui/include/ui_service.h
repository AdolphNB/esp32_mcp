#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of items the touch reorder path accepts. Keep in sync with
 * MAX_LIST_ITEMS in ui_scene.c. */
#define UI_SERVICE_LIST_ORDER_MAX 16

esp_err_t ui_service_init(void);
bool ui_service_is_ready(void);

esp_err_t ui_service_apply_scene(int page, const char *scene_json, char *err, size_t err_len);
esp_err_t ui_service_clear(int page);
esp_err_t ui_service_set_page(int page);
esp_err_t ui_service_next_page(void);
esp_err_t ui_service_notify(const char *text, const char *level, int duration_ms);
esp_err_t ui_service_set_brightness(int percent);
esp_err_t ui_service_set_backlight(bool on);
esp_err_t ui_service_refresh(void);

int ui_service_current_page(void);
int ui_service_page_count(void);
const char *ui_service_current_scene(void);
const char *ui_service_get_scene(int page);
void ui_service_status_json(cJSON *root);

/* Touch-driven updates. These only mutate the stored scene (no re-render) and
 * are executed from a small worker task so LVGL event callbacks never take the
 * ui_service lock while holding the LVGL lock. */
esp_err_t ui_service_list_set_done_async(const char *widget_id, int item_index, bool done);
esp_err_t ui_service_list_reorder_async(const char *widget_id, const int *order, int count);

#ifdef __cplusplus
}
#endif
