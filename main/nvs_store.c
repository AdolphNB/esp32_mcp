#include "nvs_store.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_store";
static nvs_handle_t s_nvs;
static char s_token[APP_TOKEN_LEN + 1];
static char s_hostname[48];

static void make_token(char out[APP_TOKEN_LEN + 1])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[APP_TOKEN_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2] = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out[APP_TOKEN_LEN] = '\0';
}

static void make_hostname(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "%s-%02x%02x",
             APP_HOSTNAME_PREFIX, mac[4], mac[5]);
}

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), reinitializing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_token);
    if (nvs_get_str(s_nvs, APP_NVS_KEY_TOKEN, s_token, &len) != ESP_OK || strlen(s_token) != APP_TOKEN_LEN) {
        make_token(s_token);
        ESP_ERROR_CHECK(nvs_set_str(s_nvs, APP_NVS_KEY_TOKEN, s_token));
        ESP_ERROR_CHECK(nvs_commit(s_nvs));
        ESP_LOGI(TAG, "Generated new MCP token (last4=%s)", s_token + APP_TOKEN_LEN - 4);
    }

    len = sizeof(s_hostname);
    if (nvs_get_str(s_nvs, APP_NVS_KEY_HOSTNAME, s_hostname, &len) != ESP_OK || s_hostname[0] == '\0') {
        make_hostname();
        ESP_ERROR_CHECK(nvs_set_str(s_nvs, APP_NVS_KEY_HOSTNAME, s_hostname));
        ESP_ERROR_CHECK(nvs_commit(s_nvs));
    }

    return ESP_OK;
}

const char *nvs_store_get_token(void)
{
    return s_token;
}

const char *nvs_store_get_hostname(void)
{
    return s_hostname;
}

bool nvs_store_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    size_t len = ssid_len;
    if (nvs_get_str(s_nvs, APP_NVS_KEY_SSID, ssid, &len) != ESP_OK || ssid[0] == '\0') {
        return false;
    }
    len = pass_len;
    if (nvs_get_str(s_nvs, APP_NVS_KEY_PASS, pass, &len) != ESP_OK) {
        pass[0] = '\0';
    }
    return true;
}

esp_err_t nvs_store_set_wifi(const char *ssid, const char *pass, bool configured)
{
    esp_err_t err = nvs_set_str(s_nvs, APP_NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(s_nvs, APP_NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(s_nvs, APP_NVS_KEY_CONFIGURED, configured ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t nvs_store_clear_wifi(void)
{
    nvs_erase_key(s_nvs, APP_NVS_KEY_SSID);
    nvs_erase_key(s_nvs, APP_NVS_KEY_PASS);
    nvs_erase_key(s_nvs, APP_NVS_KEY_CONFIGURED);
    nvs_erase_key(s_nvs, APP_NVS_KEY_PENDING);
    return nvs_commit(s_nvs);
}

esp_err_t nvs_store_clear_all(void)
{
    esp_err_t err = nvs_erase_all(s_nvs);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t nvs_store_get_str(const char *key, char *out, size_t out_len)
{
    size_t len = out_len;
    return nvs_get_str(s_nvs, key, out, &len);
}

esp_err_t nvs_store_set_str(const char *key, const char *value)
{
    esp_err_t err = nvs_set_str(s_nvs, key, value ? value : "");
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t nvs_store_get_u8(const char *key, uint8_t *out)
{
    return nvs_get_u8(s_nvs, key, out);
}

esp_err_t nvs_store_set_u8(const char *key, uint8_t value)
{
    esp_err_t err = nvs_set_u8(s_nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}
