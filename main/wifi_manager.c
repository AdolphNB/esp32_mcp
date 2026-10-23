#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_store.h"

static const char *TAG = "wifi_mgr";

#define RECONNECT_MAX_RETRIES 3

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static wifi_mgr_state_t s_state = WIFI_MGR_STATE_BOOT;
static volatile bool s_ap_active;
static volatile bool s_connected;
static char s_ip[16];
static char s_ssid[33];
static char s_pass[65];
static char s_ap_ssid[33];
static uint8_t s_bssid[6];
static int s_rssi;
static int s_channel;
static char s_auth_mode[20];
static int s_retry_count;
static char s_last_error[32];
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_ap_stop_timer;
static bool s_have_credentials;

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_have_credentials) {
        ESP_LOGI(TAG, "reconnecting...");
        esp_wifi_connect();
    }
}

static void ap_stop_timer_cb(void *arg)
{
    (void)arg;
    if (s_connected && s_ap_active) {
        ESP_LOGI(TAG, "STA connected, stopping provisioning AP");
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) {
            s_ap_active = false;
        } else {
            ESP_LOGW(TAG, "failed to stop AP: %s", esp_err_to_name(err));
        }
    }
}

static void schedule_reconnect(uint32_t delay_ms)
{
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
}

static const char *auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
    default: return "UNKNOWN";
    }
}

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    size_t n = src ? strlen(src) : 0;
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

static void update_ip(void)
{
    esp_netif_ip_info_t info = {0};
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK && info.ip.addr != 0) {
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&info.ip));
    } else {
        s_ip[0] = '\0';
    }
}

static void update_ap_info(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_rssi = ap.rssi;
        s_channel = ap.primary;
        memcpy(s_bssid, ap.bssid, sizeof(s_bssid));
        snprintf(s_auth_mode, sizeof(s_auth_mode), "%s", auth_mode_str(ap.authmode));
    } else {
        s_rssi = 0;
        s_channel = 0;
        memset(s_bssid, 0, sizeof(s_bssid));
        snprintf(s_auth_mode, sizeof(s_auth_mode), "UNKNOWN");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (s_have_credentials) {
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
            s_connected = false;
            s_ip[0] = '\0';
            update_ap_info();
            ESP_LOGW(TAG, "STA disconnected, reason=%d", disc ? disc->reason : -1);
            if (disc) {
                switch (disc->reason) {
                case WIFI_REASON_AUTH_FAIL:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_MIC_FAILURE:
                    snprintf(s_last_error, sizeof(s_last_error), "auth_failed");
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    snprintf(s_last_error, sizeof(s_last_error), "ap_not_found");
                    break;
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_ASSOC_FAIL:
                    snprintf(s_last_error, sizeof(s_last_error), "timeout");
                    break;
                default:
                    snprintf(s_last_error, sizeof(s_last_error), "disconnected");
                    break;
                }
            }

            if (!s_have_credentials) {
                s_state = WIFI_MGR_STATE_PROVISIONING;
                return;
            }

            if (s_state == WIFI_MGR_STATE_CONNECTED) {
                s_state = WIFI_MGR_STATE_CONNECTING;
                s_retry_count = 1;
                schedule_reconnect(1000);
            } else {
                s_retry_count++;
                if (s_retry_count <= RECONNECT_MAX_RETRIES) {
                    uint32_t delay = 1000U << (s_retry_count - 1);
                    if (delay > 10000U) {
                        delay = 10000U;
                    }
                    s_state = WIFI_MGR_STATE_CONNECTING;
                    schedule_reconnect(delay);
                } else {
                    ESP_LOGW(TAG, "too many failures, enabling provisioning AP");
                    s_state = WIFI_MGR_STATE_ERROR;
                    wifi_manager_start_ap();
                    schedule_reconnect(30000);
                }
            }
        } else if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "AP started, SSID=%s", s_ap_ssid);
        } else if (event_id == WIFI_EVENT_AP_STOP) {
            ESP_LOGI(TAG, "AP stopped");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        s_retry_count = 0;
        s_last_error[0] = '\0';
        s_state = WIFI_MGR_STATE_CONNECTED;
        update_ap_info();
        ESP_LOGI(TAG, "STA connected, ip=%s rssi=%d", s_ip, s_rssi);

        nvs_store_set_u8(APP_NVS_KEY_CONFIGURED, 1);
        nvs_store_set_u8(APP_NVS_KEY_PENDING, 0);

        if (s_ap_active) {
            esp_timer_stop(s_ap_stop_timer);
            esp_timer_start_once(s_ap_stop_timer, 120ULL * 1000ULL * 1000ULL);
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) {
        return ESP_FAIL;
    }
    esp_netif_set_hostname(s_sta_netif, nvs_store_get_hostname());

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    esp_timer_create_args_t targs = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect_timer));

    targs.callback = ap_stop_timer_cb;
    targs.name = "ap_stop";
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_ap_stop_timer));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%02X%02X", APP_AP_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = {0};
    copy_str((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), s_ap_ssid);
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);
    copy_str((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), APP_AP_PASSWORD);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    }
    if (err == ESP_OK) {
        s_ap_active = true;
        if (s_state != WIFI_MGR_STATE_CONNECTED) {
            s_state = WIFI_MGR_STATE_PROVISIONING;
        }
        ESP_LOGI(TAG, "provisioning AP ready: %s, portal http://192.168.4.1", s_ap_ssid);
    }
    return err;
}

static esp_err_t start_sta(void)
{
    wifi_config_t sta_cfg = {0};
    copy_str((char *)sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), s_ssid);
    copy_str((char *)sta_cfg.sta.password, sizeof(sta_cfg.sta.password), s_pass);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_mode(s_ap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    }
    if (err == ESP_OK) {
        s_state = WIFI_MGR_STATE_CONNECTING;
        s_retry_count = 0;
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t wifi_manager_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    s_have_credentials = nvs_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));

    if (s_have_credentials) {
        snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
        snprintf(s_pass, sizeof(s_pass), "%s", pass);
        ESP_LOGI(TAG, "starting STA for SSID=%s", s_ssid);
        esp_err_t err = start_sta();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "STA start failed (%s), falling back to AP", esp_err_to_name(err));
            return wifi_manager_start_ap();
        }
        return ESP_OK;
    }

    ESP_LOGI(TAG, "no Wi-Fi credentials, starting provisioning AP");
    return wifi_manager_start_ap();
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32 || (password && strlen(password) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", password ? password : "");
    s_have_credentials = true;
    s_retry_count = 0;
    s_last_error[0] = '\0';

    esp_err_t err = nvs_store_set_wifi(s_ssid, s_pass, false);
    if (err == ESP_OK) {
        err = nvs_store_set_u8(APP_NVS_KEY_PENDING, 1);
    }
    if (err != ESP_OK) {
        return err;
    }

    return start_sta();
}

esp_err_t wifi_manager_forget(void)
{
    esp_err_t err = nvs_store_clear_wifi();
    if (err == ESP_OK) {
        esp_restart();
    }
    return err;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_state;
}

const char *wifi_manager_state_str(void)
{
    switch (s_state) {
    case WIFI_MGR_STATE_PROVISIONING: return "provisioning";
    case WIFI_MGR_STATE_CONNECTING: return "connecting";
    case WIFI_MGR_STATE_CONNECTED: return "connected";
    case WIFI_MGR_STATE_ERROR: return "error";
    default: return "boot";
    }
}

bool wifi_manager_is_ap_active(void)
{
    return s_ap_active;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip(void)
{
    update_ip();
    return s_ip;
}

const char *wifi_manager_get_ssid(void)
{
    return s_ssid;
}

const char *wifi_manager_get_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *wifi_manager_get_hostname(void)
{
    return nvs_store_get_hostname();
}

int wifi_manager_get_rssi(void)
{
    return s_rssi;
}

int wifi_manager_get_channel(void)
{
    return s_channel;
}

const char *wifi_manager_get_auth_mode(void)
{
    return s_auth_mode;
}

const char *wifi_manager_get_error(void)
{
    return s_last_error;
}

const uint8_t *wifi_manager_get_bssid(void)
{
    return s_bssid;
}
