#include "captive_portal.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "json_util.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_store.h"
#include "ota.h"
#include "wifi_manager.h"

static const char *TAG = "portal";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;

static bool header_token_ok(httpd_req_t *req)
{
    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0 || len > 96) {
        return false;
    }
    char value[100] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    const char *prefix = "Bearer ";
    if (strncmp(value, prefix, strlen(prefix)) != 0) {
        return false;
    }
    return strcmp(value + strlen(prefix), nvs_store_get_token()) == 0;
}

static bool sensitive_allowed(httpd_req_t *req)
{
    return wifi_manager_is_ap_active() || header_token_ok(req);
}

static esp_err_t require_sensitive(httpd_req_t *req)
{
    if (sensitive_allowed(req)) {
        return ESP_OK;
    }
    json_send_error(req, "401 Unauthorized", "unauthorized", "Bearer token required");
    return ESP_FAIL;
}

static esp_err_t send_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return json_send_error(req, "500 Internal Server Error", "internal", "out of memory");
    }

    const char *ip = wifi_manager_get_ip();
    cJSON_AddStringToObject(root, "state", wifi_manager_state_str());
    cJSON_AddStringToObject(root, "ssid", wifi_manager_get_ssid());
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "error", wifi_manager_get_error());
    cJSON_AddNumberToObject(root, "rssi", wifi_manager_get_rssi());
    cJSON_AddStringToObject(root, "fw_version", esp_app_get_description()->version);
    cJSON_AddBoolToObject(root, "ap_active", wifi_manager_is_ap_active());
    cJSON_AddStringToObject(root, "ap_ssid", wifi_manager_get_ap_ssid());
    cJSON_AddStringToObject(root, "hostname", nvs_store_get_hostname());

    char mdns[64];
    snprintf(mdns, sizeof(mdns), "%s.local", nvs_store_get_hostname());
    cJSON_AddStringToObject(root, "mdns", mdns);

    char mcp_url[96] = "";
    if (wifi_manager_is_connected() && ip[0]) {
        snprintf(mcp_url, sizeof(mcp_url), "http://%s:%d/mcp", ip, APP_MCP_PORT);
    }
    cJSON_AddStringToObject(root, "mcp_url", mcp_url);

    if (sensitive_allowed(req)) {
        cJSON_AddStringToObject(root, "mcp_token", nvs_store_get_token());
    }

    return json_send(req, root);
}

static int read_json_body(httpd_req_t *req, cJSON **out)
{
    if (!req || !out) {
        return -1;
    }
    if (req->content_len == 0 || req->content_len > 2048) {
        return -1;
    }
    char *buf = calloc(1, req->content_len + 1);
    if (!buf) {
        return -1;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            return -1;
        }
        received += (size_t)r;
    }
    *out = cJSON_Parse(buf);
    free(buf);
    return *out ? 0 : -1;
}

static esp_err_t handler_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t handler_status(httpd_req_t *req)
{
    return send_status(req);
}

static int ap_record_cmp(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t *)b;
    return rb->rssi - ra->rssi;
}

static esp_err_t handler_scan(httpd_req_t *req)
{
    if (!sensitive_allowed(req)) {
        return json_send_error(req, "401 Unauthorized", "unauthorized", "Bearer token required");
    }
    if (!wifi_manager_is_ap_active()) {
        return json_send_error(req, "409 Conflict", "not_in_ap_mode",
                               "scan is only available while the provisioning AP is active");
    }

    wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        return json_send_error(req, "500 Internal Server Error", "scan_failed",
                               esp_err_to_name(err));
    }

    uint16_t num = 20;
    wifi_ap_record_t *records = calloc(num, sizeof(wifi_ap_record_t));
    if (!records) {
        return json_send_error(req, "500 Internal Server Error", "internal", "out of memory");
    }
    err = esp_wifi_scan_get_ap_records(&num, records);
    if (err != ESP_OK) {
        free(records);
        return json_send_error(req, "500 Internal Server Error", "scan_failed",
                               esp_err_to_name(err));
    }
    qsort(records, num, sizeof(wifi_ap_record_t), ap_record_cmp);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(records);
        return json_send_error(req, "500 Internal Server Error", "internal", "out of memory");
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    int count = 0;
    for (int i = 0; i < num && count < 20; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (records[j].ssid[0] && strcmp((char *)records[j].ssid, (char *)records[i].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "auth", records[i].authmode);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddItemToArray(arr, item);
        count++;
    }
    free(records);
    return json_send(req, root);
}

static esp_err_t handler_connect(httpd_req_t *req)
{
    if (require_sensitive(req) != ESP_OK) {
        return ESP_OK;
    }

    cJSON *root = NULL;
    if (read_json_body(req, &root) != 0) {
        return json_send_error(req, "400 Bad Request", "invalid_json", "invalid JSON body");
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !ssid->valuestring || ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return json_send_error(req, "400 Bad Request", "invalid_args", "ssid is required");
    }

    esp_err_t err = wifi_manager_connect(ssid->valuestring,
                                         cJSON_IsString(pass) ? pass->valuestring : "");
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return json_send_error(req, "400 Bad Request", "connect_failed", esp_err_to_name(err));
    }

    cJSON *reply = cJSON_CreateObject();
    if (!reply) {
        return json_send_error(req, "500 Internal Server Error", "internal", "out of memory");
    }
    cJSON_AddBoolToObject(reply, "accepted", true);
    cJSON_AddStringToObject(reply, "state", "connecting");
    return json_send(req, reply);
}

static esp_err_t handler_forget(httpd_req_t *req)
{
    if (require_sensitive(req) != ESP_OK) {
        return ESP_OK;
    }
    nvs_store_clear_wifi();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "reboot_in_ms", 1000);
    esp_err_t err = json_send(req, root);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return err;
}

static esp_err_t handler_reboot(httpd_req_t *req)
{
    if (require_sensitive(req) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "reboot_in_ms", 1000);
    esp_err_t err = json_send(req, root);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return err;
}

static esp_err_t read_header_string(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    out[0] = '\0';
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len >= out_len) {
        return ESP_ERR_NOT_FOUND;
    }
    return httpd_req_get_hdr_value_str(req, name, out, out_len);
}

static esp_err_t handler_ota_upload(httpd_req_t *req)
{
    if (!header_token_ok(req)) {
        return json_send_error(req, "401 Unauthorized", "unauthorized", "Bearer token required");
    }
    char sha[80] = "";
    char version[APP_VERSION_LEN] = "";
    read_header_string(req, "X-Firmware-SHA256", sha, sizeof(sha));
    read_header_string(req, "X-Firmware-Version", version, sizeof(version));

    esp_err_t err = ota_handle_http_upload(req, sha, version);
    if (err == ESP_ERR_INVALID_STATE) {
        return json_send_error(req, "409 Conflict", "busy", "another OTA is running");
    }
    if (err != ESP_OK) {
        return json_send_error(req, "400 Bad Request", "ota_failed", esp_err_to_name(err));
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "reboot_in_ms", 2000);
    return json_send(req, root);
}

static esp_err_t handler_ota_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return json_send_error(req, "500 Internal Server Error", "internal", "out of memory");
    }
    ota_status_json(root);
    return json_send(req, root);
}

static esp_err_t handler_ota_abort(httpd_req_t *req)
{
    if (!header_token_ok(req)) {
        return json_send_error(req, "401 Unauthorized", "unauthorized", "Bearer token required");
    }
    esp_err_t err = ota_abort();
    if (err != ESP_OK) {
        return json_send_error(req, "409 Conflict", "not_running", "no OTA in progress");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "aborted", true);
    return json_send(req, root);
}

static esp_err_t handler_captive(httpd_req_t *req)
{
    if (!wifi_manager_is_ap_active()) {
        if (strcmp(req->uri, "/generate_204") == 0) {
            httpd_resp_set_status(req, "204 No Content");
            return httpd_resp_send(req, NULL, 0);
        }
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGW(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGW(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    uint8_t resp[512];
    while (true) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 12 || !wifi_manager_is_ap_active()) {
            continue;
        }

        size_t q_end = 12;
        while (q_end < (size_t)n && buf[q_end] != 0) {
            if ((buf[q_end] & 0xc0) == 0xc0) {
                q_end += 2;
                break;
            }
            q_end += buf[q_end] + 1;
        }
        if (q_end + 5 > (size_t)n) {
            continue;
        }
        q_end++;
        uint16_t qtype = (uint16_t)((buf[q_end] << 8) | buf[q_end + 1]);
        size_t question_len = q_end + 4;
        if (question_len + 16 > sizeof(resp)) {
            continue;
        }

        memset(resp, 0, sizeof(resp));
        memcpy(resp, buf, question_len > sizeof(resp) ? sizeof(resp) : question_len);
        resp[2] = 0x81;
        resp[3] = 0x80;
        resp[6] = 0;
        resp[7] = 1;
        resp[8] = 0;
        resp[9] = 0;
        resp[10] = 0;
        resp[11] = 0;

        size_t len = question_len;
        if (qtype == 1 && len + 16 <= sizeof(resp)) {
            resp[len++] = 0xc0;
            resp[len++] = 0x0c;
            resp[len++] = 0x00;
            resp[len++] = 0x01;
            resp[len++] = 0x00;
            resp[len++] = 0x01;
            resp[len++] = 0x00;
            resp[len++] = 0x00;
            resp[len++] = 0x00;
            resp[len++] = 60;
            resp[len++] = 0x00;
            resp[len++] = 0x04;
            resp[len++] = 192;
            resp[len++] = 168;
            resp[len++] = 4;
            resp[len++] = 1;
        } else {
            resp[6] = 0;
            resp[7] = 0;
        }
        sendto(sock, resp, len, 0, (struct sockaddr *)&from, from_len);
    }
}

esp_err_t captive_portal_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = APP_WEB_PORT;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 20;
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handler_index},
        {.uri = "/ota", .method = HTTP_GET, .handler = handler_index},
        {.uri = "/api/status", .method = HTTP_GET, .handler = handler_status},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = handler_scan},
        {.uri = "/api/connect", .method = HTTP_POST, .handler = handler_connect},
        {.uri = "/api/forget", .method = HTTP_POST, .handler = handler_forget},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = handler_reboot},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = handler_ota_upload},
        {.uri = "/api/ota/status", .method = HTTP_GET, .handler = handler_ota_status},
        {.uri = "/api/ota/abort", .method = HTTP_POST, .handler = handler_ota_abort},
        {.uri = "/generate_204", .method = HTTP_GET, .handler = handler_captive},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_captive},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = handler_captive},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = handler_captive},
        {.uri = "/fwlink", .method = HTTP_GET, .handler = handler_captive},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    xTaskCreate(dns_server_task, "captive_dns", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "portal ready on port %d", APP_WEB_PORT);
    return ESP_OK;
}
