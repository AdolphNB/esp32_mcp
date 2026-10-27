#include "ui_backend.h"

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

static const char *TAG = "ui_lvgl";

/*
 * Placeholder for the LVGL/RGB backend. The real implementation will be added
 * once the 4.3B board is available for bring-up.
 */
static esp_err_t lvgl_init(void)
{
    ESP_LOGW(TAG, "LVGL backend is not implemented yet");
    return ESP_OK;
}

static esp_err_t lvgl_apply_scene(int page, const char *scene_json)
{
    (void)page;
    (void)scene_json;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t lvgl_set_brightness(int percent)
{
    (void)percent;
    return ESP_OK;
}

static esp_err_t lvgl_set_backlight(bool on)
{
    (void)on;
    return ESP_OK;
}

static esp_err_t lvgl_notify(const char *text, const char *level, int duration_ms)
{
    (void)text;
    (void)level;
    (void)duration_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

const ui_backend_t ui_backend_lvgl = {
    .name = "lvgl",
    .init = lvgl_init,
    .apply_scene = lvgl_apply_scene,
    .set_brightness = lvgl_set_brightness,
    .set_backlight = lvgl_set_backlight,
    .notify = lvgl_notify,
};

#endif /* CONFIG_APP_ENABLE_DISPLAY */
