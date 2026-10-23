#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t json_send(httpd_req_t *req, cJSON *root);
esp_err_t json_send_error(httpd_req_t *req, const char *status, const char *code, const char *message);
