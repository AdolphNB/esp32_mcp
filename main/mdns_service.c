#include "mdns_service.h"

#include "app_config.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs_store.h"

static const char *TAG = "mdns";
static bool s_started;

esp_err_t mdns_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(nvs_store_get_hostname());
    mdns_instance_name_set("ESP32-S3 MCP Node");

    mdns_txt_item_t txt[] = {
        {"path", "/mcp"},
        {"mcp", "1"},
        {"version", esp_app_get_description()->version},
        {"model", "ESP32-S3"},
    };
    err = mdns_service_add(NULL, "_http", "_tcp", APP_MCP_PORT, txt,
                           sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "advertising %s.local:%d/mcp", nvs_store_get_hostname(), APP_MCP_PORT);
    return ESP_OK;
}
