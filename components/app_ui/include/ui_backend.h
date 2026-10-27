#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*apply_scene)(int page, const char *scene_json);
    esp_err_t (*set_brightness)(int percent);
    esp_err_t (*set_backlight)(bool on);
    esp_err_t (*notify)(const char *text, const char *level, int duration_ms);
} ui_backend_t;

const ui_backend_t *ui_backend_get(void);

#ifdef __cplusplus
}
#endif
