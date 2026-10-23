#include "ota.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "nvs_store.h"

static const char *TAG = "ota";

typedef struct {
    ota_state_t state;
    int progress;
    char target_version[APP_VERSION_LEN];
    char error[APP_ERROR_LEN];
    volatile bool abort_requested;
} ota_status_t;
static ota_status_t s_status = {
    .state = OTA_STATE_IDLE,
    .progress = 0,
    .error = "",
};
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_busy_lock;
static bool s_busy;
static esp_timer_handle_t s_reboot_timer;
static esp_timer_handle_t s_health_timer;

static const char *state_str(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_DOWNLOADING: return "downloading";
    case OTA_STATE_VERIFYING: return "verifying";
    case OTA_STATE_READY: return "ready";
    case OTA_STATE_ERROR: return "error";
    default: return "idle";
    }
}

static void set_error(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_ERROR;
    snprintf(s_status.error, sizeof(s_status.error), "%s", msg ? msg : "error");
    xSemaphoreGive(s_lock);

    nvs_store_set_str(APP_NVS_KEY_OTA_ERROR, msg ? msg : "error");
    nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "failed");

    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    s_busy = false;
    xSemaphoreGive(s_busy_lock);
}

static bool valid_sha256(const char *sha)
{
    if (!sha || strlen(sha) != 64) {
        return false;
    }
    for (size_t i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)sha[i])) {
            return false;
        }
    }
    return true;
}

static void hex_digest(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[64] = '\0';
}

static int version_cmp(const char *a, const char *b)
{
    int va[3] = {0}, vb[3] = {0};
    sscanf(a ? a : "0", "%d.%d.%d", &va[0], &va[1], &va[2]);
    sscanf(b ? b : "0", "%d.%d.%d", &vb[0], &vb[1], &vb[2]);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) {
            return va[i] < vb[i] ? -1 : 1;
        }
    }
    return 0;
}

static void reboot_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(uint32_t delay_ms)
{
    if (!s_reboot_timer) {
        esp_timer_create_args_t args = {
            .callback = reboot_timer_cb,
            .name = "ota_reboot",
        };
        esp_timer_create(&args, &s_reboot_timer);
    }
    esp_timer_stop(s_reboot_timer);
    esp_timer_start_once(s_reboot_timer, (uint64_t)delay_ms * 1000ULL);
}

static void health_timer_cb(void *arg)
{
    (void)arg;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "OTA health check passed, marking app valid");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "success");
            nvs_store_set_u8(APP_NVS_KEY_OTA_PENDING, 0);
        } else {
            ESP_LOGE(TAG, "mark valid failed: %s", esp_err_to_name(err));
        }
    }
}

void ota_health_check_start(void)
{
    if (!s_health_timer) {
        esp_timer_create_args_t args = {
            .callback = health_timer_cb,
            .name = "ota_health",
        };
        if (esp_timer_create(&args, &s_health_timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_health_timer);
    esp_timer_start_once(s_health_timer, (uint64_t)CONFIG_APP_OTA_HEALTH_TIMEOUT_MS * 1000ULL);
}

esp_err_t ota_init(void)
{
    psa_crypto_init();
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        s_busy_lock = xSemaphoreCreateMutex();
    }
    if (!s_lock || !s_busy_lock) {
        return ESP_ERR_NO_MEM;
    }

    char last[APP_ERROR_LEN] = "none";
    nvs_store_get_str(APP_NVS_KEY_OTA_RESULT, last, sizeof(last));

    uint8_t pending = 0;
    nvs_store_get_u8(APP_NVS_KEY_OTA_PENDING, &pending);
    char target[APP_VERSION_LEN] = "";
    nvs_store_get_str(APP_NVS_KEY_OTA_TARGET, target, sizeof(target));

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running) {
        esp_ota_get_state_partition(running, &state);
    }
    bool running_is_target = target[0] &&
        strcmp(esp_app_get_description()->version, target) == 0;

    if (pending) {
        if (running_is_target) {
            if (state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "running OTA image is pending verification");
                ota_health_check_start();
            } else {
                ESP_LOGI(TAG, "OTA target is running and already valid");
                nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "success");
                nvs_store_set_u8(APP_NVS_KEY_OTA_PENDING, 0);
            }
        } else {
            ESP_LOGW(TAG, "OTA did not validate; bootloader rolled back");
            nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "rolled_back");
            nvs_store_set_u8(APP_NVS_KEY_OTA_PENDING, 0);
        }
    } else if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "image is pending verification (USB flash?)");
        ota_health_check_start();
    }

    ESP_LOGI(TAG, "running partition=%s version=%s last_result=%s",
             running ? running->label : "?",
             esp_app_get_description()->version,
             last);
    return ESP_OK;
}

bool ota_is_busy(void)
{
    bool busy;
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    busy = s_busy;
    xSemaphoreGive(s_busy_lock);
    return busy;
}

static bool claim_busy(void)
{
    bool ok = false;
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    if (!s_busy) {
        s_busy = true;
        ok = true;
    }
    xSemaphoreGive(s_busy_lock);
    return ok;
}

static void release_busy(void)
{
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    s_busy = false;
    xSemaphoreGive(s_busy_lock);
}

void ota_status_json(cJSON *out)
{
    if (!out) {
        return;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    bool pending_verify = false;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY) {
        pending_verify = true;
    }

    char last[APP_ERROR_LEN] = "none";
    nvs_store_get_str(APP_NVS_KEY_OTA_RESULT, last, sizeof(last));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddStringToObject(out, "state", state_str(s_status.state));
    cJSON_AddNumberToObject(out, "progress", s_status.progress);
    cJSON_AddStringToObject(out, "target_version", s_status.target_version);
    cJSON_AddStringToObject(out, "error", s_status.error);
    xSemaphoreGive(s_lock);

    cJSON_AddStringToObject(out, "running_partition", running ? running->label : "");
    cJSON_AddStringToObject(out, "next_partition", next ? next->label : "");
    cJSON_AddStringToObject(out, "running_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(out, "last_result", last);
    cJSON_AddBoolToObject(out, "rollback_pending", pending_verify);
}

static esp_err_t validate_apply(const char *url, const char *sha256, const char *version, const char *confirm)
{
    if (!url || !sha256 || !version || !confirm) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(confirm, "OTA") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!valid_sha256(sha256)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (const char *p = url; *p; p++) {
        if ((unsigned char)*p < 0x20) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (const char *p = version; *p; p++) {
        if ((unsigned char)*p < 0x20) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    const char *prefix = CONFIG_APP_OTA_ALLOWED_URL_PREFIX;
    if (prefix[0] && strncmp(url, prefix, strlen(prefix)) != 0) {
        ESP_LOGW(TAG, "URL rejected by allowlist: %s", url);
        return ESP_ERR_INVALID_ARG;
    }
    if (version_cmp(version, esp_app_get_description()->version) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_APP_OTA_ALLOW_DOWNGRADE
    if (version_cmp(version, esp_app_get_description()->version) < 0) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return ESP_OK;
}

static esp_err_t run_download(const char *url, const char *sha256, const char *version)
{
    char expected[65];
    snprintf(expected, sizeof(expected), "%s", sha256);
    for (int i = 0; i < 64; i++) {
        expected[i] = (char)tolower((unsigned char)expected[i]);
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
    };
    if (strncmp(url, "https://", 8) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "OTA HTTP status=%d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    if (content_length < 0) {
        content_length = 0;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    if (content_length > 0 && (size_t)content_length > part->size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    err = esp_ota_begin(part, content_length > 0 ? (size_t)content_length : OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&sha, PSA_ALG_SHA_256);

    uint8_t *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(handle);
        psa_hash_abort(&sha);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    err = ESP_OK;
    while (true) {
        if (s_status.abort_requested) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        int r = esp_http_client_read(client, (char *)buf, 4096);
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        if (esp_ota_write(handle, buf, (size_t)r) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        psa_hash_update(&sha, buf, (size_t)r);
        received += r;
        if (content_length > 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.progress = (received * 100) / content_length;
            xSemaphoreGive(s_lock);
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        esp_ota_abort(handle);
        psa_hash_abort(&sha);
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_VERIFYING;
    s_status.progress = 100;
    xSemaphoreGive(s_lock);

    uint8_t digest[32] = {0};
    char actual[65];
    size_t digest_len = 0;
    psa_hash_finish(&sha, digest, sizeof(digest), &digest_len);
    hex_digest(digest, actual);

    if (strcmp(actual, expected) != 0) {
        esp_ota_abort(handle);
        ESP_LOGE(TAG, "SHA256 mismatch: expected=%s actual=%s", expected, actual);
        return ESP_ERR_INVALID_CRC;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        return err;
    }

    esp_app_desc_t desc;
    char image_version[APP_VERSION_LEN] = "";
    err = esp_ota_get_partition_description(part, &desc);
    if (err == ESP_OK) {
        snprintf(image_version, sizeof(image_version), "%s", desc.version);
        ESP_LOGI(TAG, "downloaded image version=%s", desc.version);
        if (version[0] && strcmp(desc.version, version) != 0) {
            ESP_LOGE(TAG, "image version mismatch: got=%s expected=%s", desc.version, version);
            return ESP_ERR_INVALID_VERSION;
        }
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return err;
    }

    nvs_store_set_str(APP_NVS_KEY_OTA_TARGET,
                      image_version[0] ? image_version : (version ? version : ""));
    nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "pending");
    nvs_store_set_u8(APP_NVS_KEY_OTA_PENDING, 1);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_READY;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "OTA ready, rebooting in 2 seconds");
    schedule_reboot(2000);
    return ESP_OK;
}

static void ota_task(void *arg)
{
    char *url = (char *)arg;
    char *sha = strchr(url, '\n');
    if (!sha) {
        free(url);
        release_busy();
        return;
    }
    *sha++ = '\0';
    char *version = strchr(sha, '\n');
    if (!version) {
        free(url);
        release_busy();
        return;
    }
    *version++ = '\0';

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.abort_requested = false;
    xSemaphoreGive(s_lock);

    esp_err_t err = run_download(url, sha, version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        set_error(esp_err_to_name(err));
    }
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_apply_start(const char *url, const char *sha256, const char *version, const char *confirm)
{
    esp_err_t err = validate_apply(url, sha256, version, confirm);
    if (err != ESP_OK) {
        return err;
    }
    if (!claim_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t len = strlen(url) + strlen(sha256) + strlen(version) + 4;
    char *arg = malloc(len);
    if (!arg) {
        release_busy();
        return ESP_ERR_NO_MEM;
    }
    snprintf(arg, len, "%s\n%s\n%s", url, sha256, version);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_DOWNLOADING;
    s_status.progress = 0;
    s_status.error[0] = '\0';
    snprintf(s_status.target_version, sizeof(s_status.target_version), "%s", version);
    xSemaphoreGive(s_lock);

    if (xTaskCreate(ota_task, "ota_task", 8192, arg, 5, NULL) != pdPASS) {
        free(arg);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.state = OTA_STATE_IDLE;
        snprintf(s_status.error, sizeof(s_status.error), "no_mem");
        xSemaphoreGive(s_lock);
        release_busy();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_abort(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = s_status.state == OTA_STATE_DOWNLOADING;
    if (active) {
        s_status.abort_requested = true;
    }
    xSemaphoreGive(s_lock);
    return active ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t ota_handle_http_upload(httpd_req_t *req, const char *sha256, const char *version)
{
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sha256 && sha256[0] && !valid_sha256(sha256)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!claim_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    char clen[24] = {0};
    size_t hlen = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (hlen == 0 || hlen >= sizeof(clen) ||
            httpd_req_get_hdr_value_str(req, "Content-Length", clen, sizeof(clen)) != ESP_OK) {
        release_busy();
        return ESP_ERR_INVALID_ARG;
    }
    long content_length = strtol(clen, NULL, 10);
    if (content_length <= 0 || content_length > 4 * 1024 * 1024) {
        release_busy();
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || (size_t)content_length > part->size) {
        release_busy();
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, (size_t)content_length, &handle);
    if (err != ESP_OK) {
        release_busy();
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_DOWNLOADING;
    s_status.progress = 0;
    s_status.abort_requested = false;
    s_status.error[0] = '\0';
    snprintf(s_status.target_version, sizeof(s_status.target_version), "%s", version ? version : "");
    xSemaphoreGive(s_lock);

    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&sha, PSA_ALG_SHA_256);

    uint8_t *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(handle);
        psa_hash_abort(&sha);
        set_error("no_mem");
        return ESP_ERR_NO_MEM;
    }

    long received = 0;
    err = ESP_OK;
    while (received < content_length) {
        if (s_status.abort_requested) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        int r = httpd_req_recv(req, (char *)buf, 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            err = ESP_FAIL;
            break;
        }
        if (esp_ota_write(handle, buf, (size_t)r) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        psa_hash_update(&sha, buf, (size_t)r);
        received += r;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.progress = (int)((received * 100) / content_length);
        xSemaphoreGive(s_lock);
    }
    free(buf);

    if (err != ESP_OK) {
        esp_ota_abort(handle);
        psa_hash_abort(&sha);
        set_error(esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_VERIFYING;
    s_status.progress = 100;
    xSemaphoreGive(s_lock);

    uint8_t digest[32] = {0};
    char actual[65];
    size_t digest_len = 0;
    psa_hash_finish(&sha, digest, sizeof(digest), &digest_len);
    hex_digest(digest, actual);

    if (sha256 && sha256[0]) {
        char expected[65];
        snprintf(expected, sizeof(expected), "%s", sha256);
        for (int i = 0; i < 64; i++) {
            expected[i] = (char)tolower((unsigned char)expected[i]);
        }
        if (strcmp(actual, expected) != 0) {
            esp_ota_abort(handle);
            set_error("sha256_mismatch");
            return ESP_ERR_INVALID_CRC;
        }
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        set_error(esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t desc;
    char image_version[APP_VERSION_LEN] = "";
    err = esp_ota_get_partition_description(part, &desc);
    if (err == ESP_OK) {
        snprintf(image_version, sizeof(image_version), "%s", desc.version);
        ESP_LOGI(TAG, "uploaded image version=%s sha256=%s", desc.version, actual);
        if (version && version[0] && strcmp(desc.version, version) != 0) {
            set_error("version_mismatch");
            return ESP_ERR_INVALID_VERSION;
        }
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        set_error(esp_err_to_name(err));
        return err;
    }

    nvs_store_set_str(APP_NVS_KEY_OTA_TARGET,
                      image_version[0] ? image_version : (version ? version : ""));
    nvs_store_set_str(APP_NVS_KEY_OTA_RESULT, "pending");
    nvs_store_set_u8(APP_NVS_KEY_OTA_PENDING, 1);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = OTA_STATE_READY;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "upload complete, rebooting in 2 seconds");
    schedule_reboot(2000);
    return ESP_OK;
}
