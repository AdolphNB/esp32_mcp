#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_mcp_tool.h"

esp_err_t mcp_tool_reply(esp_mcp_tool_result_t *result, cJSON *root, bool is_error);
esp_err_t mcp_tool_reply_error(esp_mcp_tool_result_t *result, const char *code, const char *message);
