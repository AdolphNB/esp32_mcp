#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

typedef enum {
    WIFI_MGR_STATE_BOOT = 0,
    WIFI_MGR_STATE_PROVISIONING,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_ERROR,
} wifi_mgr_state_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);

wifi_mgr_state_t wifi_manager_get_state(void);
const char *wifi_manager_state_str(void);

bool wifi_manager_is_ap_active(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_ip(void);
const char *wifi_manager_get_ssid(void);
const char *wifi_manager_get_ap_ssid(void);
const char *wifi_manager_get_hostname(void);
int wifi_manager_get_rssi(void);
int wifi_manager_get_channel(void);
const char *wifi_manager_get_auth_mode(void);
const char *wifi_manager_get_error(void);
const uint8_t *wifi_manager_get_bssid(void);

esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_forget(void);

/* Starts the provisioning AP (used when no credentials or after failures). */
esp_err_t wifi_manager_start_ap(void);
