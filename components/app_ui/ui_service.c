#include "ui_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "board.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ui_backend.h"
#include "ui_scene.h"

#if CONFIG_APP_ENABLE_DISPLAY

static const char *TAG = "ui_service";

#define UI_NVS_NAMESPACE "ui"
#define UI_DIR "/spiffs/ui"

typedef struct {
    char *json;
    size_t len;
} ui_page_t;

static ui_page_t s_pages[CONFIG_APP_DISPLAY_MAX_PAGES];
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ready;
static bool s_fs_ready;
static int s_current;
static int s_brightness;
static bool s_backlight;
static bool s_ready;
static char s_last_error[128];

static void set_last_error(const char *msg)
{
    const char *src = msg ? msg : "";
    size_t n = strlen(src);
    if (n >= sizeof(s_last_error)) {
        n = sizeof(s_last_error) - 1;
    }
    memcpy(s_last_error, src, n);
    s_last_error[n] = '\0';
}

static void nvs_load(void)
{
    s_current = 0;
    s_brightness = CONFIG_APP_DISPLAY_DEFAULT_BRIGHTNESS;
    s_backlight = true;
    if (!s_nvs_ready) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(s_nvs, "page", &v) == ESP_OK) {
        s_current = v;
    }
    if (nvs_get_u8(s_nvs, "bright", &v) == ESP_OK) {
        s_brightness = v;
    }
    if (nvs_get_u8(s_nvs, "bl", &v) == ESP_OK) {
        s_backlight = v != 0;
    }
    if (s_current < 0 || s_current >= CONFIG_APP_DISPLAY_MAX_PAGES) {
        s_current = 0;
    }
    if (s_brightness > 100) {
        s_brightness = 100;
    }
}

static void nvs_save_u8(const char *key, uint8_t value)
{
    if (!s_nvs_ready) {
        return;
    }
    if (nvs_set_u8(s_nvs, key, value) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size <= 0 || size > CONFIG_APP_DISPLAY_MAX_SCENE_BYTES) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    if (out_len) {
        *out_len = (size_t)size;
    }
    return buf;
}

static esp_err_t write_file(const char *path, const char *data, size_t len)
{
    if (!s_fs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    mkdir(UI_DIR, 0777);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    return n == len ? ESP_OK : ESP_FAIL;
}

static void page_path(int page, char *out, size_t out_len)
{
    snprintf(out, out_len, UI_DIR "/page%d.json", page);
}

static esp_err_t store_scene_locked(int page, const char *json, size_t len, const char *err_prefix)
{
    char *normalized = NULL;
    char err[96] = {0};
    esp_err_t e = ui_scene_validate(json, &normalized, err, sizeof(err));
    if (e != ESP_OK) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: %s", err_prefix ? err_prefix : "invalid scene", err);
        set_last_error(msg);
        return e;
    }

    free(s_pages[page].json);
    s_pages[page].json = normalized;
    s_pages[page].len = strlen(normalized);

    char path[48];
    page_path(page, path, sizeof(path));
    esp_err_t werr = write_file(path, s_pages[page].json, s_pages[page].len);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "failed to persist page %d: %s", page, esp_err_to_name(werr));
    }
    set_last_error("");
    return ESP_OK;
}

static esp_err_t apply_current_locked(void)
{
    const ui_backend_t *backend = ui_backend_get();
    if (!backend || !backend->apply_scene) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pages[s_current].json) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = backend->apply_scene(s_current, s_pages[s_current].json);
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "backend apply failed: %s", esp_err_to_name(err));
        set_last_error(msg);
    }
    return err;
}

esp_err_t ui_service_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_current = 0;
    s_last_error[0] = '\0';

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
    }
    if (nvs_open(UI_NVS_NAMESPACE, NVS_READWRITE, &s_nvs) == ESP_OK) {
        s_nvs_ready = true;
    }
    nvs_load();

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    ESP_LOGI(TAG, "mounting SPIFFS (first boot may format the storage partition)");
    err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_fs_ready = true;
        mkdir(UI_DIR, 0777);
        ESP_LOGI(TAG, "SPIFFS ready");
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s (RAM-only)", esp_err_to_name(err));
    }

    for (int i = 0; i < CONFIG_APP_DISPLAY_MAX_PAGES; i++) {
        char path[48];
        page_path(i, path, sizeof(path));
        size_t len = 0;
        char *raw = s_fs_ready ? read_file(path, &len) : NULL;
        if (!raw) {
            continue;
        }
        char *normalized = NULL;
        char werr[96] = {0};
        if (ui_scene_validate(raw, &normalized, werr, sizeof(werr)) == ESP_OK) {
            s_pages[i].json = normalized;
            s_pages[i].len = strlen(normalized);
        } else {
            ESP_LOGW(TAG, "page %d invalid: %s", i, werr);
        }
        free(raw);
    }

    if (!s_pages[0].json) {
        char *home = NULL;
        if (ui_scene_default_home(&home) == ESP_OK && home) {
            esp_err_t herr = store_scene_locked(0, home, strlen(home), "default home");
            if (herr != ESP_OK) {
                ESP_LOGE(TAG, "default home scene rejected: %s", s_last_error);
            }
            ui_scene_free(home);
        } else {
            ESP_LOGE(TAG, "failed to build default home scene");
        }
    }
    if (!s_pages[0].json) {
        ESP_LOGE(TAG, "no scene on page 0 after init");
    }

    if (!s_pages[s_current].json) {
        for (int i = 0; i < CONFIG_APP_DISPLAY_MAX_PAGES; i++) {
            if (s_pages[i].json) {
                s_current = i;
                break;
            }
        }
    }

    const ui_backend_t *backend = ui_backend_get();
    if (backend && backend->init) {
        esp_err_t berr = backend->init();
        if (berr != ESP_OK) {
            ESP_LOGE(TAG, "backend init failed: %s", esp_err_to_name(berr));
            set_last_error("display backend init failed");
            return berr;
        }
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(3000)) == pdTRUE) {
        esp_err_t aerr = apply_current_locked();
        if (aerr != ESP_OK) {
            ESP_LOGE(TAG, "apply current scene failed: %s", esp_err_to_name(aerr));
        } else {
            ESP_LOGI(TAG, "current scene applied: page=%d bytes=%u", s_current,
                     (unsigned)s_pages[s_current].len);
        }
        if (backend && backend->set_brightness) {
            backend->set_brightness(s_brightness);
        }
        if (backend && backend->set_backlight) {
            backend->set_backlight(s_backlight);
        }
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGE(TAG, "ui lock timeout during init");
    }

    s_ready = true;
    ESP_LOGI(TAG, "ready, page=%d brightness=%d fs=%s backend=%s",
             s_current, s_brightness, s_fs_ready ? "yes" : "no",
             backend ? backend->name : "none");
    return ESP_OK;
}

esp_err_t ui_service_apply_scene(int page, const char *scene_json, char *err, size_t err_len)
{
    if (!scene_json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (page < 0 || page >= CONFIG_APP_DISPLAY_MAX_PAGES) {
        if (err && err_len) {
            snprintf(err, err_len, "page must be 0..%d", CONFIG_APP_DISPLAY_MAX_PAGES - 1);
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        if (err && err_len) {
            snprintf(err, err_len, "busy");
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t werr = store_scene_locked(page, scene_json, strlen(scene_json), "render");
    if (werr == ESP_OK) {
        s_current = page;
        nvs_save_u8("page", (uint8_t)page);
        werr = apply_current_locked();
    }

    if (werr != ESP_OK && err && err_len && s_last_error[0]) {
        snprintf(err, err_len, "%s", s_last_error);
    }
    xSemaphoreGive(s_lock);
    return werr;
}

esp_err_t ui_service_clear(int page)
{
    if (page < 0 || page >= CONFIG_APP_DISPLAY_MAX_PAGES) {
        return ESP_ERR_INVALID_ARG;
    }
    return ui_service_apply_scene(page, "{\"v\":1,\"theme\":\"dark\",\"widgets\":[]}", NULL, 0);
}

esp_err_t ui_service_set_page(int page)
{
    if (page < 0 || page >= CONFIG_APP_DISPLAY_MAX_PAGES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_pages[page].json) {
        s_current = page;
        nvs_save_u8("page", (uint8_t)page);
        err = apply_current_locked();
    } else {
        err = ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_service_next_page(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int next = s_current;
    for (int i = 1; i <= CONFIG_APP_DISPLAY_MAX_PAGES; i++) {
        int candidate = (s_current + i) % CONFIG_APP_DISPLAY_MAX_PAGES;
        if (s_pages[candidate].json) {
            next = candidate;
            break;
        }
    }
    if (next == s_current) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    s_current = next;
    nvs_save_u8("page", (uint8_t)next);
    esp_err_t err = apply_current_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_service_notify(const char *text, const char *level, int duration_ms)
{
    if (!text || !text[0] || strlen(text) > 256) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duration_ms <= 0 || duration_ms > 60000) {
        duration_ms = 3000;
    }
    if (level && strcmp(level, "info") != 0 && strcmp(level, "success") != 0 &&
        strcmp(level, "warn") != 0 && strcmp(level, "error") != 0) {
        level = "info";
    }
    const ui_backend_t *backend = ui_backend_get();
    if (!backend || !backend->notify) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return backend->notify(text, level ? level : "info", duration_ms);
}

esp_err_t ui_service_set_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    nvs_save_u8("bright", (uint8_t)percent);
    s_backlight = percent > 0;
    nvs_save_u8("bl", s_backlight ? 1 : 0);
    const ui_backend_t *backend = ui_backend_get();
    if (backend && backend->set_brightness) {
        backend->set_brightness(percent);
    }
    if (backend && backend->set_backlight) {
        backend->set_backlight(s_backlight);
    }
    return ESP_OK;
}

esp_err_t ui_service_set_backlight(bool on)
{
    s_backlight = on;
    nvs_save_u8("bl", on ? 1 : 0);
    const ui_backend_t *backend = ui_backend_get();
    if (backend && backend->set_backlight) {
        return backend->set_backlight(on);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ui_service_refresh(void)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = apply_current_locked();
    xSemaphoreGive(s_lock);
    return err;
}

bool ui_service_is_ready(void)
{
    return s_ready;
}

int ui_service_current_page(void)
{
    return s_current;
}

int ui_service_page_count(void)
{
    return CONFIG_APP_DISPLAY_MAX_PAGES;
}

const char *ui_service_current_scene(void)
{
    return s_pages[s_current].json;
}

const char *ui_service_get_scene(int page)
{
    if (page < 0 || page >= CONFIG_APP_DISPLAY_MAX_PAGES) {
        return NULL;
    }
    return s_pages[page].json;
}

void ui_service_status_json(cJSON *root)
{
    if (!root) {
        return;
    }
    const ui_backend_t *backend = ui_backend_get();
    cJSON_AddNumberToObject(root, "current_page", s_current);
    cJSON_AddNumberToObject(root, "max_pages", CONFIG_APP_DISPLAY_MAX_PAGES);
    cJSON_AddNumberToObject(root, "brightness", s_brightness);
    cJSON_AddBoolToObject(root, "backlight", s_backlight);
    cJSON_AddStringToObject(root, "backend", backend ? backend->name : "none");
    cJSON_AddStringToObject(root, "filesystem", s_fs_ready ? "spiffs" : "ram");
    cJSON_AddStringToObject(root, "last_error", s_last_error);
    cJSON *pages = cJSON_AddArrayToObject(root, "pages");
    for (int i = 0; i < CONFIG_APP_DISPLAY_MAX_PAGES; i++) {
        cJSON *p = cJSON_CreateObject();
        if (!p) {
            break;
        }
        cJSON_AddNumberToObject(p, "index", i);
        cJSON_AddBoolToObject(p, "has_scene", s_pages[i].json != NULL);
        cJSON_AddNumberToObject(p, "bytes", s_pages[i].len);
        cJSON_AddBoolToObject(p, "current", i == s_current);
        cJSON_AddItemToArray(pages, p);
    }
    if (s_pages[s_current].json) {
        cJSON_AddNumberToObject(root, "current_bytes", s_pages[s_current].len);
    }
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */
