#include "mcp_tools_common.h"

#include "esp_log.h"

static const char *TAG = "mcp_tools";

esp_err_t mcp_tool_reply(esp_mcp_tool_result_t *result, cJSON *root, bool is_error)
{
    if (!result) {
        if (root) {
            cJSON_Delete(root);
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "JSON allocation failed");
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return mcp_tool_reply_error(result, "internal", "JSON serialization failed");
    }

    esp_err_t err = esp_mcp_tool_result_add_text(result, json);
    if (err == ESP_OK) {
        err = esp_mcp_tool_result_set_structured_json(result, json);
    }
    if (err == ESP_OK) {
        err = esp_mcp_tool_result_set_is_error(result, is_error);
    }
    cJSON_free(json);
    return err;
}

esp_err_t mcp_tool_reply_error(esp_mcp_tool_result_t *result, const char *code, const char *message)
{
    ESP_LOGW(TAG, "tool error: %s (%s)", code ? code : "?", message ? message : "");
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "error", code ? code : "internal");
    cJSON_AddStringToObject(root, "message", message ? message : "");
    return mcp_tool_reply(result, root, true);
}
