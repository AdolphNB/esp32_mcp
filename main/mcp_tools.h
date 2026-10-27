#pragma once

#include "cJSON.h"
#include "esp_mcp_engine.h"

void mcp_register_device_tools(esp_mcp_t *mcp);
void mcp_register_gpio_tools(esp_mcp_t *mcp);
void mcp_register_ota_tools(esp_mcp_t *mcp);
void mcp_register_display_tools(esp_mcp_t *mcp);
void mcp_register_io_tools(esp_mcp_t *mcp);
void mcp_register_resources(esp_mcp_t *mcp);

void mcp_device_info_fill(cJSON *root);
void mcp_gpio_snapshot_fill(cJSON *root);
