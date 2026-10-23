#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY,
    OTA_STATE_ERROR,
} ota_state_t;

esp_err_t ota_init(void);
void ota_health_check_start(void);

esp_err_t ota_apply_start(const char *url, const char *sha256, const char *version, const char *confirm);
esp_err_t ota_abort(void);
bool ota_is_busy(void);
void ota_status_json(cJSON *out);

/* Handles a raw application/octet-stream upload from the portal HTTP server. */
esp_err_t ota_handle_http_upload(httpd_req_t *req, const char *sha256, const char *version);
