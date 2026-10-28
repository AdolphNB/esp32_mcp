#include "mcp_server.h"

#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_mgr.h"
#include "mcp_tools.h"
#include "nvs_store.h"
#if CONFIG_APP_ENABLE_DISPLAY
#include "ui_service.h"
#endif

static const char *TAG = "mcp_server";

static esp_mcp_t *s_mcp;
static esp_mcp_mgr_handle_t s_mgr;
static bool s_started;
static bool s_failed;

/*
 * Strong override of the weak hook in mcp-c-sdk. This gives every device its own
 * runtime token from NVS instead of the compile-time CONFIG token.
 */
const char *esp_mcp_http_get_bearer_token(void)
{
    return nvs_store_get_token();
}

esp_err_t mcp_server_init(void)
{
    if (s_mcp) {
        return ESP_OK;
    }

    esp_err_t err = esp_mcp_create(&s_mcp);
    if (err != ESP_OK) {
        return err;
    }

    esp_mcp_set_server_info(s_mcp,
        "ESP32-S3 MCP Node",
#if CONFIG_APP_ENABLE_DISPLAY
        "Wi-Fi connected ESP32-S3 desk display exposing UI, calendar, todo, GPIO, "
        "isolated IO and OTA tools.",
#else
        "Wi-Fi connected ESP32-S3 device exposing status, GPIO, WS2812 and OTA tools.",
#endif
        NULL, NULL);

    esp_mcp_set_instructions(s_mcp,
        "This device controls only GPIO pins listed by gpio_list. "
        "Prefer read-only tools first. GPIO writes are physically actuating. "
#if CONFIG_APP_ENABLE_DISPLAY
        "Use display_render for custom screens, or display_calendar/display_todo/"
        "display_quote/display_horoscope for designed templates. "
        "display_info reports the current page and clock. "
#endif
        "ota_apply is destructive: it downloads firmware from an allowlisted LAN URL, "
        "requires the exact SHA256 and confirm=OTA, then reboots the device.");

    mcp_register_device_tools(s_mcp);
    mcp_register_gpio_tools(s_mcp);
    mcp_register_ota_tools(s_mcp);
#if CONFIG_APP_ENABLE_DISPLAY
    if (ui_service_is_ready()) {
        mcp_register_display_tools(s_mcp);
        mcp_register_io_tools(s_mcp);
    }
#endif
    mcp_register_resources(s_mcp);

    ESP_LOGI(TAG, "MCP engine initialized");
    return ESP_OK;
}

esp_err_t mcp_server_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (s_failed) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = APP_MCP_PORT;
    http_cfg.ctrl_port = 32769; /* portal uses the default 32768 */
    http_cfg.max_open_sockets = 8;
    http_cfg.max_uri_handlers = 12;
    http_cfg.stack_size = 12288;
    http_cfg.lru_purge_enable = true;

    esp_mcp_mgr_config_t cfg = {
        .transport = esp_mcp_transport_http_server,
        .config = &http_cfg,
        .instance = s_mcp,
    };

    esp_err_t err = esp_mcp_mgr_init(cfg, &s_mgr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mcp_mgr_init failed: %s", esp_err_to_name(err));
        s_failed = true;
        return err;
    }
    err = esp_mcp_mgr_start(s_mgr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mcp server start failed: %s", esp_err_to_name(err));
        s_failed = true;
        return err;
    }
    err = esp_mcp_mgr_register_endpoint(s_mgr, "mcp", s_mcp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register endpoint failed: %s", esp_err_to_name(err));
        esp_mcp_mgr_stop(s_mgr);
        s_failed = true;
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "MCP Streamable HTTP ready on port %d, path /mcp", APP_MCP_PORT);
    return ESP_OK;
}

bool mcp_server_is_started(void)
{
    return s_started;
}
