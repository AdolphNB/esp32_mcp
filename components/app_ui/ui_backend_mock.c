#include "ui_backend.h"

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

static const char *TAG = "ui_mock";

static esp_err_t mock_init(void)
{
    ESP_LOGI(TAG, "mock display backend initialized");
    return ESP_OK;
}

static esp_err_t mock_apply_scene(int page, const char *scene_json)
{
    int widgets = 0;
    cJSON *root = scene_json ? cJSON_Parse(scene_json) : NULL;
    if (root) {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        if (cJSON_IsArray(arr)) {
            widgets = cJSON_GetArraySize(arr);
        }
        cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "page=%d bytes=%u widgets=%d", page,
             scene_json ? (unsigned)strlen(scene_json) : 0, widgets);
    return ESP_OK;
}

static esp_err_t mock_set_brightness(int percent)
{
    ESP_LOGI(TAG, "brightness=%d", percent);
    return ESP_OK;
}

static esp_err_t mock_set_backlight(bool on)
{
    ESP_LOGI(TAG, "backlight=%s", on ? "on" : "off");
    return ESP_OK;
}

static esp_err_t mock_notify(const char *text, const char *level, int duration_ms)
{
    ESP_LOGI(TAG, "notify[%s] %s (%d ms)", level, text, duration_ms);
    return ESP_OK;
}

const ui_backend_t ui_backend_mock = {
    .name = "mock",
    .init = mock_init,
    .apply_scene = mock_apply_scene,
    .set_brightness = mock_set_brightness,
    .set_backlight = mock_set_backlight,
    .notify = mock_notify,
};

#endif /* CONFIG_APP_ENABLE_DISPLAY */
