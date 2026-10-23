#include "json_util.h"

#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "json_util";

esp_err_t json_send(httpd_req_t *req, cJSON *root)
{
    if (!req) {
        if (root) {
            cJSON_Delete(root);
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!root) {
        return json_send_error(req, "500 Internal Server Error", "internal", "json allocation failed");
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return json_send_error(req, "500 Internal Server Error", "internal", "json serialization failed");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
    }
    cJSON_free(body);
    return err;
}

esp_err_t json_send_error(httpd_req_t *req, const char *status, const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", code ? code : "internal");
    cJSON_AddStringToObject(root, "message", message ? message : "");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_status(req, status ? status : "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return err;
}
