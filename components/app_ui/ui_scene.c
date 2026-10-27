#include "ui_scene.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "cJSON.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

#define MAX_WIDGETS 24
#define MAX_TEXT_LEN 512
#define MAX_LIST_ITEMS 16
#define MAX_EVENTS 8
#define MAX_JSON_DEPTH 16

static void set_err(char *err, size_t err_len, const char *fmt, ...)
{
    if (!err || err_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

static bool known_type(const char *type)
{
    static const char *types[] = {
        "text", "big_text", "clock", "calendar", "list", "quote",
        "horoscope", "image", "qr", "divider", "progress",
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(type, types[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int json_depth(const char *s)
{
    int depth = 0;
    int max_depth = 0;
    bool in_string = false;
    bool escape = false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            depth++;
            if (depth > max_depth) {
                max_depth = depth;
            }
        } else if (c == '}' || c == ']') {
            if (depth > 0) {
                depth--;
            }
        }
    }
    return max_depth;
}

static bool get_string_limited(const cJSON *item, const char *name, size_t max_len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, name);
    if (!v) {
        return true;
    }
    if (!cJSON_IsString(v) || !v->valuestring) {
        return false;
    }
    return strlen(v->valuestring) <= max_len;
}

static void set_number(cJSON *obj, const char *key, double value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item) {
        cJSON_SetNumberValue(item, value);
    } else {
        cJSON_AddNumberToObject(obj, key, value);
    }
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static esp_err_t validate_widget(cJSON *w, int index, int width, int height, char *err, size_t err_len)
{
    const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(w, "type");
    if (!cJSON_IsString(type_item) || !type_item->valuestring || !type_item->valuestring[0]) {
        set_err(err, err_len, "widgets[%d].type is required", index);
        return ESP_ERR_INVALID_ARG;
    }
    const char *type = type_item->valuestring;
    if (!known_type(type)) {
        set_err(err, err_len, "widgets[%d].type '%s' is not supported", index, type);
        return ESP_ERR_INVALID_ARG;
    }

    const char *id = NULL;
    const cJSON *id_item = cJSON_GetObjectItemCaseSensitive(w, "id");
    if (id_item) {
        if (!cJSON_IsString(id_item) || !id_item->valuestring) {
            set_err(err, err_len, "widgets[%d].id must be a string", index);
            return ESP_ERR_INVALID_ARG;
        }
        id = id_item->valuestring;
    }

    const cJSON *x_item = cJSON_GetObjectItemCaseSensitive(w, "x");
    const cJSON *y_item = cJSON_GetObjectItemCaseSensitive(w, "y");
    const cJSON *w_item = cJSON_GetObjectItemCaseSensitive(w, "w");
    const cJSON *h_item = cJSON_GetObjectItemCaseSensitive(w, "h");
    if (!cJSON_IsNumber(x_item) || !cJSON_IsNumber(y_item) ||
        !cJSON_IsNumber(w_item) || !cJSON_IsNumber(h_item)) {
        set_err(err, err_len, "widgets[%d].x/y/w/h are required numbers", index);
        return ESP_ERR_INVALID_ARG;
    }

    int x = clamp_int((int)x_item->valuedouble, 0, width - 1);
    int y = clamp_int((int)y_item->valuedouble, 0, height - 1);
    int ww = clamp_int((int)w_item->valuedouble, 1, width - x);
    int hh = clamp_int((int)h_item->valuedouble, 1, height - y);
    set_number(w, "x", x);
    set_number(w, "y", y);
    set_number(w, "w", ww);
    set_number(w, "h", hh);

    if (!id || !id[0]) {
        char generated[16];
        snprintf(generated, sizeof(generated), "w%d", index);
        cJSON_AddStringToObject(w, "id", generated);
    }

    if (strcmp(type, "text") == 0 || strcmp(type, "big_text") == 0) {
        if (!get_string_limited(w, "text", MAX_TEXT_LEN)) {
            set_err(err, err_len, "widgets[%d].text must be a string <= %d bytes", index, MAX_TEXT_LEN);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "clock") == 0) {
        if (!get_string_limited(w, "format", 16)) {
            set_err(err, err_len, "widgets[%d].format must be <= 16 bytes", index);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "calendar") == 0) {
        if (!get_string_limited(w, "solar", 32) || !get_string_limited(w, "weekday", 32) ||
            !get_string_limited(w, "lunar", 64) || !get_string_limited(w, "ganzhi", 64) ||
            !get_string_limited(w, "festival", 64) || !get_string_limited(w, "yi", 64) ||
            !get_string_limited(w, "ji", 64)) {
            set_err(err, err_len, "widgets[%d] calendar field too long", index);
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *events = cJSON_GetObjectItemCaseSensitive(w, "events");
        if (events) {
            if (!cJSON_IsArray(events) || cJSON_GetArraySize(events) > MAX_EVENTS) {
                set_err(err, err_len, "widgets[%d].events must be an array <= %d", index, MAX_EVENTS);
                return ESP_ERR_INVALID_ARG;
            }
            const cJSON *e = NULL;
            cJSON_ArrayForEach(e, events) {
                if (!cJSON_IsString(e) || strlen(e->valuestring) > 128) {
                    set_err(err, err_len, "widgets[%d].events entries must be strings <= 128", index);
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    } else if (strcmp(type, "list") == 0) {
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(w, "items");
        if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > MAX_LIST_ITEMS) {
            set_err(err, err_len, "widgets[%d].items must be an array <= %d", index, MAX_LIST_ITEMS);
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, items) {
            const cJSON *text = cJSON_GetObjectItemCaseSensitive(it, "text");
            if (!cJSON_IsObject(it) || !cJSON_IsString(text) || !text->valuestring ||
                strlen(text->valuestring) > 128) {
                set_err(err, err_len, "widgets[%d].items entries need text <= 128", index);
                return ESP_ERR_INVALID_ARG;
            }
            const cJSON *prio = cJSON_GetObjectItemCaseSensitive(it, "prio");
            if (prio && (!cJSON_IsString(prio) ||
                         (strcmp(prio->valuestring, "high") != 0 &&
                          strcmp(prio->valuestring, "normal") != 0 &&
                          strcmp(prio->valuestring, "low") != 0))) {
                set_err(err, err_len, "widgets[%d].items[].prio must be high/normal/low", index);
                return ESP_ERR_INVALID_ARG;
            }
        }
    } else if (strcmp(type, "quote") == 0) {
        if (!get_string_limited(w, "text", 256) || !get_string_limited(w, "author", 64) ||
            !get_string_limited(w, "source", 64)) {
            set_err(err, err_len, "widgets[%d] quote text/author/source too long", index);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "horoscope") == 0) {
        if (!get_string_limited(w, "sign", 32) || !get_string_limited(w, "date", 32) ||
            !get_string_limited(w, "summary", 256) || !get_string_limited(w, "lucky_color", 32) ||
            !get_string_limited(w, "lucky_number", 16)) {
            set_err(err, err_len, "widgets[%d] horoscope field too long", index);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "image") == 0) {
        if (!get_string_limited(w, "url", 512)) {
            set_err(err, err_len, "widgets[%d].url must be <= 512 bytes", index);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "qr") == 0) {
        if (!get_string_limited(w, "text", 256)) {
            set_err(err, err_len, "widgets[%d].text must be <= 256 bytes", index);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(type, "progress") == 0) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(w, "value");
        if (value && !cJSON_IsNumber(value)) {
            set_err(err, err_len, "widgets[%d].value must be a number", index);
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

esp_err_t ui_scene_validate(const char *json, char **normalized, char *err, size_t err_len)
{
    if (!json || !normalized) {
        return ESP_ERR_INVALID_ARG;
    }
    *normalized = NULL;
    size_t len = strlen(json);
    if (len == 0) {
        set_err(err, err_len, "scene is empty");
        return ESP_ERR_INVALID_ARG;
    }
    if (len > CONFIG_APP_DISPLAY_MAX_SCENE_BYTES) {
        set_err(err, err_len, "scene exceeds %d bytes", CONFIG_APP_DISPLAY_MAX_SCENE_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }
    if (json_depth(json) > MAX_JSON_DEPTH) {
        set_err(err, err_len, "scene nesting is too deep");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        set_err(err, err_len, "invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        set_err(err, err_len, "scene must be a JSON object");
        return ESP_ERR_INVALID_ARG;
    }

    const board_desc_t *board = board_get();
    int width = board && board->width > 0 ? board->width : 800;
    int height = board && board->height > 0 ? board->height : 480;

    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (v && (!cJSON_IsNumber(v) || (int)v->valuedouble != 1)) {
        cJSON_Delete(root);
        set_err(err, err_len, "scene version must be 1");
        return ESP_ERR_INVALID_ARG;
    }
    set_number(root, "v", 1);

    cJSON *theme = cJSON_GetObjectItemCaseSensitive(root, "theme");
    if (theme && !cJSON_IsString(theme)) {
        cJSON_Delete(root);
        set_err(err, err_len, "theme must be a string");
        return ESP_ERR_INVALID_ARG;
    }
    if (!theme) {
        cJSON_AddStringToObject(root, "theme", "dark");
    }

    cJSON *screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
    if (!screen) {
        screen = cJSON_AddObjectToObject(root, "screen");
    }
    if (!cJSON_IsObject(screen)) {
        cJSON_Delete(root);
        set_err(err, err_len, "screen must be an object");
        return ESP_ERR_INVALID_ARG;
    }
    set_number(screen, "w", width);
    set_number(screen, "h", height);

    cJSON *widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    if (!cJSON_IsArray(widgets)) {
        cJSON_Delete(root);
        set_err(err, err_len, "widgets must be an array");
        return ESP_ERR_INVALID_ARG;
    }
    int count = cJSON_GetArraySize(widgets);
    if (count > MAX_WIDGETS) {
        cJSON_Delete(root);
        set_err(err, err_len, "widgets exceeds %d", MAX_WIDGETS);
        return ESP_ERR_INVALID_ARG;
    }

    int index = 0;
    cJSON *w = NULL;
    cJSON_ArrayForEach(w, widgets) {
        if (!cJSON_IsObject(w)) {
            cJSON_Delete(root);
            set_err(err, err_len, "widgets[%d] must be an object", index);
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t werr = validate_widget(w, index, width, height, err, err_len);
        if (werr != ESP_OK) {
            cJSON_Delete(root);
            return werr;
        }
        index++;
    }

    *normalized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*normalized) {
        set_err(err, err_len, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ui_scene_free(char *json)
{
    if (json) {
        cJSON_free(json);
    }
}

esp_err_t ui_scene_default_home(char **out_json)
{
    if (!out_json) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "theme", "dark");
    cJSON *screen = cJSON_AddObjectToObject(root, "screen");
    const board_desc_t *board = board_get();
    cJSON_AddNumberToObject(screen, "w", board && board->width > 0 ? board->width : 800);
    cJSON_AddNumberToObject(screen, "h", board && board->height > 0 ? board->height : 480);
    cJSON *widgets = cJSON_AddArrayToObject(root, "widgets");

    cJSON *w = cJSON_CreateObject();
    cJSON_AddStringToObject(w, "id", "clock");
    cJSON_AddStringToObject(w, "type", "clock");
    cJSON_AddNumberToObject(w, "x", 24);
    cJSON_AddNumberToObject(w, "y", 16);
    cJSON_AddNumberToObject(w, "w", 360);
    cJSON_AddNumberToObject(w, "h", 90);
    cJSON_AddNumberToObject(w, "size", 64);
    cJSON_AddStringToObject(w, "format", "%H:%M");
    cJSON_AddItemToArray(widgets, w);

    w = cJSON_CreateObject();
    cJSON_AddStringToObject(w, "id", "date");
    cJSON_AddStringToObject(w, "type", "calendar");
    cJSON_AddNumberToObject(w, "x", 24);
    cJSON_AddNumberToObject(w, "y", 120);
    cJSON_AddNumberToObject(w, "w", 360);
    cJSON_AddNumberToObject(w, "h", 320);
    cJSON_AddStringToObject(w, "solar", "--");
    cJSON_AddStringToObject(w, "weekday", "");
    cJSON_AddStringToObject(w, "lunar", "--");
    cJSON_AddStringToObject(w, "ganzhi", "");
    cJSON_AddItemToArray(widgets, w);

    w = cJSON_CreateObject();
    cJSON_AddStringToObject(w, "id", "hint");
    cJSON_AddStringToObject(w, "type", "text");
    cJSON_AddNumberToObject(w, "x", 408);
    cJSON_AddNumberToObject(w, "y", 160);
    cJSON_AddNumberToObject(w, "w", 368);
    cJSON_AddNumberToObject(w, "h", 120);
    cJSON_AddStringToObject(w, "text", "等待 AI 推送…");
    cJSON_AddNumberToObject(w, "size", 28);
    cJSON_AddStringToObject(w, "align", "center");
    cJSON_AddItemToArray(widgets, w);

    *out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*out_json) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */
