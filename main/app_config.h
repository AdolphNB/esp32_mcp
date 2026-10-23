#pragma once

#include "sdkconfig.h"

#define APP_FW_VERSION        CONFIG_APP_FW_VERSION
#define APP_NVS_NAMESPACE     "app_cfg"
#define APP_MCP_PORT          CONFIG_APP_MCP_PORT
#define APP_WEB_PORT          CONFIG_APP_WEB_PORT
#define APP_AP_PREFIX         CONFIG_APP_AP_PREFIX
#define APP_AP_PASSWORD       CONFIG_APP_AP_PASSWORD
#define APP_HOSTNAME_PREFIX   CONFIG_APP_HOSTNAME_PREFIX
#define APP_GPIO_ALLOWLIST    CONFIG_APP_GPIO_ALLOWLIST
#define APP_LED_GPIO          CONFIG_APP_LED_GPIO

#define APP_NVS_KEY_SSID          "wifi_ssid"
#define APP_NVS_KEY_PASS          "wifi_pass"
#define APP_NVS_KEY_CONFIGURED    "wifi_cfg"
#define APP_NVS_KEY_PENDING       "wifi_pend"
#define APP_NVS_KEY_TOKEN         "mcp_token"
#define APP_NVS_KEY_HOSTNAME      "hostname"
#define APP_NVS_KEY_OTA_PENDING   "ota_pend"
#define APP_NVS_KEY_OTA_TARGET    "ota_target"
#define APP_NVS_KEY_OTA_RESULT    "ota_result"
#define APP_NVS_KEY_OTA_VERSION   "ota_version"
#define APP_NVS_KEY_OTA_ERROR     "ota_error"

#define APP_TOKEN_LEN 32
#define APP_VERSION_LEN 32
#define APP_ERROR_LEN 48
