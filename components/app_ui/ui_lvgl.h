#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ui_lvgl_lock(uint32_t timeout_ms);
void ui_lvgl_unlock(void);
lv_display_t *ui_lvgl_display(void);
const lv_font_t *ui_lvgl_cjk_font(void);

void ui_lvgl_request_scene_refresh(void);

#ifdef __cplusplus
}
#endif
