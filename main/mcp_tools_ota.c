#include "mcp_tools.h"

#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "mcp_tools_common.h"
#include "ota.h"
#include "rate_limit.h"

static esp_err_t ota_status_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    ota_status_json(root);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t ota_apply_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
#if !CONFIG_APP_MCP_ALLOW_OTA
    return mcp_tool_reply_error(result, "ota_disabled", "MCP OTA is disabled in firmware config");
#else
    const char *url = esp_mcp_property_list_get_property_string(properties, "url");
    const char *sha256 = esp_mcp_property_list_get_property_string(properties, "sha256");
    const char *version = esp_mcp_property_list_get_property_string(properties, "version");
    const char *confirm = esp_mcp_property_list_get_property_string(properties, "confirm");

    if (!url || !sha256 || !version || !confirm) {
        return mcp_tool_reply_error(result, "invalid_args",
                                    "url, sha256, version and confirm are required");
    }
    if (strcmp(confirm, "OTA") != 0) {
        return mcp_tool_reply_error(result, "invalid_args", "confirm must be the literal OTA");
    }

    esp_err_t err = ota_apply_start(url, sha256, version, confirm);
    if (err == ESP_ERR_INVALID_ARG) {
        return mcp_tool_reply_error(result, "invalid_args",
                                    "URL must match the allowlist and SHA256 must be 64 hex chars");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        if (ota_is_busy()) {
            return mcp_tool_reply_error(result, "busy", "another OTA is already running");
        }
        return mcp_tool_reply_error(result, "version_rejected",
                                    "same version, downgrade, or OTA not allowed");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "internal", esp_err_to_name(err));
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "accepted", true);
    cJSON_AddStringToObject(root, "state", "downloading");
    cJSON_AddStringToObject(root, "target_version", version);
    return mcp_tool_reply(result, root, false);
#endif
}

static esp_err_t ota_abort_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    esp_err_t err = ota_abort();
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "not_running", "no OTA download is in progress");
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "aborted", true);
    return mcp_tool_reply(result, root, false);
}

void mcp_register_ota_tools(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }

    esp_mcp_tool_t *tool = esp_mcp_tool_create_ex("ota_status", "OTA Status",
        "Return OTA state, progress, running/next partition, version and last result.", ota_status_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

#if CONFIG_APP_MCP_ALLOW_OTA
    tool = esp_mcp_tool_create_ex("ota_apply", "OTA Apply",
        "Download and flash firmware from an allowlisted LAN URL. Arguments: url, sha256 (64 hex), version, confirm (literal OTA).",
        ota_apply_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create("url", ESP_MCP_PROPERTY_TYPE_STRING));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create("sha256", ESP_MCP_PROPERTY_TYPE_STRING));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create("version", ESP_MCP_PROPERTY_TYPE_STRING));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create("confirm", ESP_MCP_PROPERTY_TYPE_STRING));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false,\"openWorldHint\":true}");
        esp_mcp_add_tool(mcp, tool);
    }
#endif

    tool = esp_mcp_tool_create_ex("ota_abort", "OTA Abort",
        "Abort an in-progress OTA download.", ota_abort_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }
}
