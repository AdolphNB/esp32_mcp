#include "mcp_tools.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mcp_tools_common.h"
#include "nvs_store.h"
#include "wifi_manager.h"

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
    }
}

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32: return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    default: return "ESP32";
    }
}

static void mac_str(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mcp_device_info_fill(cJSON *root)
{
    if (!root) {
        return;
    }
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_s[18];
    mac_str(mac, mac_s);

    const esp_app_desc_t *app = esp_app_get_description();

    cJSON_AddStringToObject(root, "model", chip_model_str(chip.model));
    cJSON_AddNumberToObject(root, "chip_revision", chip.revision);
    cJSON_AddNumberToObject(root, "cores", chip.cores);
    cJSON_AddStringToObject(root, "mac", mac_s);
    cJSON_AddStringToObject(root, "firmware", app ? app->version : APP_FW_VERSION);
    cJSON_AddStringToObject(root, "idf", app ? app->idf_ver : IDF_VER);
    cJSON_AddStringToObject(root, "mcp_sdk", "2.0.1");
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000ULL);
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(esp_reset_reason()));
    cJSON_AddNumberToObject(root, "flash_size_kb", flash_size / 1024);
    cJSON_AddNumberToObject(root, "free_heap_kb", esp_get_free_heap_size() / 1024);
    cJSON_AddStringToObject(root, "hostname", nvs_store_get_hostname());
    const board_desc_t *board = board_get();
    if (board) {
        cJSON_AddStringToObject(root, "board", board->id);
        cJSON_AddStringToObject(root, "board_name", board->name);
        cJSON_AddBoolToObject(root, "display", board->has_display);
        cJSON_AddBoolToObject(root, "rtc", board->has_rtc);
        cJSON_AddBoolToObject(root, "isolated_io", board->has_isolated_io);
    }
}

static esp_err_t device_info_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    mcp_device_info_fill(root);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t sys_status_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000ULL);
    cJSON_AddNumberToObject(root, "free_heap_kb", esp_get_free_heap_size() / 1024);
    cJSON_AddNumberToObject(root, "min_free_heap_kb", esp_get_minimum_free_heap_size() / 1024);
    cJSON_AddNumberToObject(root, "largest_free_block_kb",
                            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
    cJSON_AddNumberToObject(root, "task_count", uxTaskGetNumberOfTasks());
    cJSON_AddNumberToObject(root, "cpu_freq_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(esp_reset_reason()));
    return mcp_tool_reply(result, root, false);
}

static esp_err_t wifi_status_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }

    cJSON_AddBoolToObject(root, "connected", wifi_manager_is_connected());
    cJSON_AddStringToObject(root, "ssid", wifi_manager_get_ssid());
    char bssid[18] = "";
    mac_str(wifi_manager_get_bssid(), bssid);
    cJSON_AddStringToObject(root, "bssid", bssid);
    cJSON_AddStringToObject(root, "ip", wifi_manager_get_ip());
    cJSON_AddNumberToObject(root, "rssi", wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "channel", wifi_manager_get_channel());
    cJSON_AddStringToObject(root, "auth_mode", wifi_manager_get_auth_mode());
    cJSON_AddStringToObject(root, "hostname", nvs_store_get_hostname());

    char mdns[64];
    snprintf(mdns, sizeof(mdns), "%s.local", nvs_store_get_hostname());
    cJSON_AddStringToObject(root, "mdns", mdns);

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t info = {0};
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            char buf[16];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.netmask));
            cJSON_AddStringToObject(root, "netmask", buf);
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.gw));
            cJSON_AddStringToObject(root, "gateway", buf);
        }
    }

    return mcp_tool_reply(result, root, false);
}

static void add_readonly_tool(esp_mcp_t *mcp, const char *name, const char *desc,
                              esp_mcp_tool_callback_ex_t cb)
{
    esp_mcp_tool_t *tool = esp_mcp_tool_create_ex(name, name, desc, cb);
    if (!tool) {
        return;
    }
    esp_mcp_tool_set_annotations_json(tool,
        "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
    esp_mcp_add_tool(mcp, tool);
}

void mcp_register_device_tools(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }
    add_readonly_tool(mcp, "device_info",
        "Return chip model, MAC, firmware version, flash size, uptime and free heap.", device_info_cb);
    add_readonly_tool(mcp, "sys_status",
        "Return heap usage, task count, CPU frequency and reset reason.", sys_status_cb);
    add_readonly_tool(mcp, "wifi_status",
        "Return Wi-Fi connection state, SSID, IP, RSSI, channel and mDNS name.", wifi_status_cb);
}
