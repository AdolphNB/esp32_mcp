#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int rssi;
    int auth;
} provisioning_scan_item_t;

typedef struct {
    char state[24];
    char ssid[33];
    char ip[16];
    char error[64];
    bool ap_active;
    char ap_ssid[33];
    char ap_password[64];
    char token[33];
} provisioning_status_t;

typedef struct {
    esp_err_t (*scan_start)(void);
    bool (*scan_busy)(void);
    int (*scan_get)(provisioning_scan_item_t *out, int max);
    esp_err_t (*connect)(const char *ssid, const char *password);
    esp_err_t (*forget)(void);
    void (*get_status)(provisioning_status_t *out);
    esp_err_t (*start_softap)(void);
} provisioning_ui_backend_t;

esp_err_t provisioning_ui_init(const provisioning_ui_backend_t *backend);
void provisioning_ui_show(void);
void provisioning_ui_hide(void);
bool provisioning_ui_is_visible(void);

#ifdef __cplusplus
}
#endif
