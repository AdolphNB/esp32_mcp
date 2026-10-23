#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t mcp_server_init(void);
esp_err_t mcp_server_start(void);
bool mcp_server_is_started(void);
