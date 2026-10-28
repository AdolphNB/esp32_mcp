#include "provisioning_backend.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_store.h"
#include "provisioning_ui.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "prov_backend";

#if CONFIG_APP_PROVISIONING_SCREEN && CONFIG_APP_DISPLAY_BACKEND_LVGL

static SemaphoreHandle_t s_lock;
static wifi_scan_item_t s_scan[CONFIG_APP_WIFI_SCAN_MAX];
static int s_scan_count;
static volatile bool s_scan_busy;
static bool s_ready;

static void scan_task(void *arg)
{
    (void)arg;
    wifi_scan_item_t tmp[CONFIG_APP_WIFI_SCAN_MAX];
    int n = 0;
    esp_err_t err = wifi_manager_scan(tmp, CONFIG_APP_WIFI_SCAN_MAX, &n);
    if (err == ESP_OK) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            memcpy(s_scan, tmp, sizeof(tmp));
            s_scan_count = n;
            xSemaphoreGive(s_lock);
        }
    } else {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_scan_count = 0;
            xSemaphoreGive(s_lock);
        }
    }
    s_scan_busy = false;
    vTaskDelete(NULL);
}

static esp_err_t scan_start_cb(void)
{
    if (s_scan_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_busy = true;
    BaseType_t ok = xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        s_scan_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool scan_busy_cb(void)
{
    return s_scan_busy;
}

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    const char *s = src ? src : "";
    size_t n = strlen(s);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static int scan_get_cb(provisioning_scan_item_t *out, int max)
{
    if (!out || max <= 0 || !s_lock) {
        return 0;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return 0;
    }
    int n = s_scan_count < max ? s_scan_count : max;
    for (int i = 0; i < n; i++) {
        copy_str(out[i].ssid, sizeof(out[i].ssid), s_scan[i].ssid);
        out[i].rssi = s_scan[i].rssi;
        out[i].auth = s_scan[i].auth;
    }
    xSemaphoreGive(s_lock);
    return n;
}

static esp_err_t connect_cb(const char *ssid, const char *password)
{
    return wifi_manager_connect(ssid, password);
}

static esp_err_t forget_cb(void)
{
    return wifi_manager_forget();
}

static void get_status_cb(provisioning_status_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    copy_str(out->state, sizeof(out->state), wifi_manager_state_str());
    copy_str(out->ssid, sizeof(out->ssid), wifi_manager_get_ssid());
    copy_str(out->ip, sizeof(out->ip), wifi_manager_get_ip());
    copy_str(out->error, sizeof(out->error), wifi_manager_get_error());
    out->ap_active = wifi_manager_is_ap_active();
    copy_str(out->ap_ssid, sizeof(out->ap_ssid), wifi_manager_get_ap_ssid());
    copy_str(out->ap_password, sizeof(out->ap_password), CONFIG_APP_AP_PASSWORD);
    copy_str(out->token, sizeof(out->token), nvs_store_get_token());
    snprintf(out->mdns_url, sizeof(out->mdns_url), "http://%s.local:%d/mcp",
             nvs_store_get_hostname(), APP_MCP_PORT);
    if (strcmp(out->state, "connected") == 0 && out->ip[0]) {
        snprintf(out->mcp_url, sizeof(out->mcp_url), "http://%s:%d/mcp",
                 out->ip, APP_MCP_PORT);
        snprintf(out->web_url, sizeof(out->web_url), "http://%s/", out->ip);
    }
}

static esp_err_t softap_cb(void)
{
    return wifi_manager_start_ap();
}

static const provisioning_ui_backend_t s_backend = {
    .scan_start = scan_start_cb,
    .scan_busy = scan_busy_cb,
    .scan_get = scan_get_cb,
    .connect = connect_cb,
    .forget = forget_cb,
    .get_status = get_status_cb,
    .start_softap = softap_cb,
};

static void monitor_task(void *arg)
{
    (void)arg;
    bool shown = false;
    wifi_mgr_state_t last = WIFI_MGR_STATE_BOOT;
    while (true) {
        wifi_mgr_state_t st = wifi_manager_get_state();
        bool visible = provisioning_ui_is_visible();

        if (st != last) {
            ESP_LOGI(TAG, "wifi state -> %s (ui_visible=%d)", wifi_manager_state_str(), visible);
            last = st;
        }
        if (!visible) {
            shown = false;
        }
        if (!shown && (st == WIFI_MGR_STATE_PROVISIONING || st == WIFI_MGR_STATE_ERROR)) {
            ESP_LOGI(TAG, "opening provisioning UI");
            provisioning_ui_show();
            shown = true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t provisioning_backend_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = provisioning_ui_init(&s_backend);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "provisioning UI init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "on-screen provisioning backend ready");
    return ESP_OK;
}

void provisioning_backend_start_task(void)
{
    if (s_ready) {
        xTaskCreate(monitor_task, "prov_monitor", 3072, NULL, 3, NULL);
    }
}

#else /* CONFIG_APP_PROVISIONING_SCREEN */

esp_err_t provisioning_backend_init(void)
{
    return ESP_OK;
}

void provisioning_backend_start_task(void)
{
}

#endif
