#include "mcp_tools.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "board.h"
#include "clock_service.h"
#include "lunar.h"
#include "mcp_tools_common.h"
#include "rate_limit.h"
#include "ui_service.h"

#if CONFIG_APP_ENABLE_DISPLAY

static int current_or_arg_page(const esp_mcp_property_list_t *properties)
{
    int page = esp_mcp_property_list_get_property_int(properties, "page");
    if (page < 0) {
        page = ui_service_current_page();
    }
    return page;
}

static cJSON *new_scene(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "theme", "dark");
    const board_desc_t *board = board_get();
    cJSON *screen = cJSON_AddObjectToObject(root, "screen");
    cJSON_AddNumberToObject(screen, "w", board && board->width > 0 ? board->width : 800);
    cJSON_AddNumberToObject(screen, "h", board && board->height > 0 ? board->height : 480);
    cJSON_AddArrayToObject(root, "widgets");
    return root;
}

static cJSON *add_widget(cJSON *scene, const char *id, const char *type,
                         int x, int y, int w, int h)
{
    if (!scene) {
        return NULL;
    }
    cJSON *widgets = cJSON_GetObjectItemCaseSensitive(scene, "widgets");
    cJSON *wgt = cJSON_CreateObject();
    if (!widgets || !wgt) {
        cJSON_Delete(wgt);
        return NULL;
    }
    cJSON_AddStringToObject(wgt, "id", id);
    cJSON_AddStringToObject(wgt, "type", type);
    cJSON_AddNumberToObject(wgt, "x", x);
    cJSON_AddNumberToObject(wgt, "y", y);
    cJSON_AddNumberToObject(wgt, "w", w);
    cJSON_AddNumberToObject(wgt, "h", h);
    cJSON_AddItemToArray(widgets, wgt);
    return wgt;
}

static esp_err_t apply_scene(esp_mcp_tool_result_t *result, int page, cJSON *scene,
                             const char *action)
{
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "scene allocation failed");
    }
    char *json = cJSON_PrintUnformatted(scene);
    cJSON_Delete(scene);
    if (!json) {
        return mcp_tool_reply_error(result, "internal", "scene serialization failed");
    }
    char err[128] = {0};
    esp_err_t e = ui_service_apply_scene(page, json, err, sizeof(err));
    cJSON_free(json);
    if (e != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", err[0] ? err : esp_err_to_name(e));
    }
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "page", page);
    cJSON_AddStringToObject(out, "action", action ? action : "render");
    return mcp_tool_reply(result, out, false);
}

static const char *weekday_cn(int wday)
{
    static const char *names[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    return (wday >= 0 && wday <= 6) ? names[wday] : "";
}

static void today_defaults(char *solar, size_t solar_len, char *weekday, size_t weekday_len,
                           char *lunar_text, size_t lunar_len, char *ganzhi, size_t ganzhi_len,
                           char *festival, size_t festival_len)
{
    time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local)) {
        strftime(solar, solar_len, "%m/%d", &local);
        snprintf(weekday, weekday_len, "%s", weekday_cn(local.tm_wday));
        const char *sf = lunar_solar_festival(local.tm_mon + 1, local.tm_mday);
        snprintf(festival, festival_len, "%s", sf);
    }
    lunar_date_t lunar;
    if (lunar_from_time(now, &lunar) == ESP_OK && lunar.valid) {
        snprintf(lunar_text, lunar_len, "%s", lunar.text);
        snprintf(ganzhi, ganzhi_len, "%s", lunar.year_ganzhi);
        if (lunar.festival[0]) {
            snprintf(festival, festival_len, "%s", lunar.festival);
        }
    }
}

static esp_err_t display_info_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    ui_service_status_json(root);
    cJSON *clock = cJSON_AddObjectToObject(root, "clock");
    clock_service_status_json(clock);
    cJSON *board = cJSON_AddObjectToObject(root, "board");
    board_info_json(board);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t display_render_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    const char *scene_json = esp_mcp_property_list_get_property_object(properties, "scene");
    if (!scene_json) {
        scene_json = esp_mcp_property_list_get_property_string(properties, "scene_json");
    }
    if (!scene_json || !scene_json[0]) {
        return mcp_tool_reply_error(result, "invalid_args", "scene object is required");
    }
    int page = current_or_arg_page(properties);
    char err[128] = {0};
    esp_err_t e = ui_service_apply_scene(page, scene_json, err, sizeof(err));
    if (e != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", err[0] ? err : esp_err_to_name(e));
    }
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "page", page);
    cJSON_AddStringToObject(out, "action", "render");
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_patch_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *patch_json = esp_mcp_property_list_get_property_object(properties, "widget");
    if (!patch_json || !patch_json[0]) {
        return mcp_tool_reply_error(result, "invalid_args", "widget object is required");
    }
    const char *base_json = ui_service_get_scene(page);
    if (!base_json) {
        return mcp_tool_reply_error(result, "display_error", "page has no scene");
    }
    cJSON *patch = cJSON_Parse(patch_json);
    cJSON *root = cJSON_Parse(base_json);
    if (!cJSON_IsObject(patch) || !cJSON_IsObject(root)) {
        cJSON_Delete(patch);
        cJSON_Delete(root);
        return mcp_tool_reply_error(result, "invalid_args", "invalid widget/scene JSON");
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(patch, "id");
    if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0]) {
        cJSON_Delete(patch);
        cJSON_Delete(root);
        return mcp_tool_reply_error(result, "invalid_args", "widget.id is required");
    }
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "%s", id->valuestring);
    cJSON *widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    cJSON *found = NULL;
    cJSON *w = NULL;
    cJSON_ArrayForEach(w, widgets) {
        const cJSON *wid = cJSON_GetObjectItemCaseSensitive(w, "id");
        if (cJSON_IsString(wid) && strcmp(wid->valuestring, id_buf) == 0) {
            found = w;
            break;
        }
    }
    if (found) {
        cJSON *merged = cJSON_Duplicate(found, true);
        if (!merged) {
            cJSON_Delete(patch);
            cJSON_Delete(root);
            return mcp_tool_reply_error(result, "internal", "out of memory");
        }
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, patch) {
            if (!child->string) {
                continue;
            }
            cJSON_DeleteItemFromObjectCaseSensitive(merged, child->string);
            cJSON_AddItemToObject(merged, child->string, cJSON_Duplicate(child, true));
        }
        cJSON_ReplaceItemViaPointer(widgets, found, merged);
    } else {
        cJSON *copy = cJSON_Duplicate(patch, true);
        if (!copy) {
            cJSON_Delete(patch);
            cJSON_Delete(root);
            return mcp_tool_reply_error(result, "internal", "out of memory");
        }
        cJSON_AddItemToArray(widgets, copy);
    }
    cJSON_Delete(patch);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return mcp_tool_reply_error(result, "internal", "scene serialization failed");
    }
    char err[128] = {0};
    esp_err_t e = ui_service_apply_scene(page, json, err, sizeof(err));
    cJSON_free(json);
    if (e != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", err[0] ? err : esp_err_to_name(e));
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "page", page);
    cJSON_AddStringToObject(out, "widget", id_buf);
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_page_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    const char *action = esp_mcp_property_list_get_property_string(properties, "action");
    esp_err_t err = ESP_OK;
    if (action && strcmp(action, "next") == 0) {
        err = ui_service_next_page();
    } else if (action && strcmp(action, "prev") == 0) {
        int prev = ui_service_current_page() - 1;
        if (prev < 0) {
            prev = ui_service_page_count() - 1;
        }
        err = ui_service_set_page(prev);
    } else {
        int page = current_or_arg_page(properties);
        err = ui_service_set_page(page);
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", esp_err_to_name(err));
    }
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "page", ui_service_current_page());
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_clear_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    esp_err_t err = ui_service_clear(page);
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", esp_err_to_name(err));
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "page", page);
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_notify_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    const char *text = esp_mcp_property_list_get_property_string(properties, "text");
    const char *level = esp_mcp_property_list_get_property_string(properties, "level");
    int duration_ms = esp_mcp_property_list_get_property_int(properties, "duration_ms");
    if (!text || !text[0]) {
        return mcp_tool_reply_error(result, "invalid_args", "text is required");
    }
    esp_err_t err = ui_service_notify(text, level, duration_ms);
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", esp_err_to_name(err));
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "text", text);
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_brightness_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int percent = esp_mcp_property_list_get_property_int(properties, "percent");
    esp_err_t err = ui_service_set_brightness(percent);
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "display_error", esp_err_to_name(err));
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "percent", percent);
    return mcp_tool_reply(result, out, false);
}

static esp_err_t display_calendar_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *solar = esp_mcp_property_list_get_property_string(properties, "solar");
    const char *weekday = esp_mcp_property_list_get_property_string(properties, "weekday");
    const char *lunar = esp_mcp_property_list_get_property_string(properties, "lunar");
    const char *ganzhi = esp_mcp_property_list_get_property_string(properties, "ganzhi");
    const char *festival = esp_mcp_property_list_get_property_string(properties, "festival");
    const char *yi = esp_mcp_property_list_get_property_string(properties, "yi");
    const char *ji = esp_mcp_property_list_get_property_string(properties, "ji");
    const char *events_json = esp_mcp_property_list_get_property_array(properties, "events");

    char solar_buf[16] = {0};
    char weekday_buf[16] = {0};
    char lunar_buf[32] = {0};
    char ganzhi_buf[16] = {0};
    char festival_buf[32] = {0};
    today_defaults(solar_buf, sizeof(solar_buf), weekday_buf, sizeof(weekday_buf),
                   lunar_buf, sizeof(lunar_buf), ganzhi_buf, sizeof(ganzhi_buf),
                   festival_buf, sizeof(festival_buf));

    cJSON *scene = new_scene();
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON *clock = add_widget(scene, "clock", "clock", 24, 16, 360, 90);
    if (clock) {
        cJSON_AddNumberToObject(clock, "size", 56);
        cJSON_AddStringToObject(clock, "format", "%H:%M");
    }
    cJSON *cal = add_widget(scene, "calendar", "calendar", 24, 112, 360, 344);
    if (cal) {
        cJSON_AddStringToObject(cal, "solar", solar && solar[0] ? solar : solar_buf);
        cJSON_AddStringToObject(cal, "weekday", weekday && weekday[0] ? weekday : weekday_buf);
        cJSON_AddStringToObject(cal, "lunar", lunar && lunar[0] ? lunar : lunar_buf);
        cJSON_AddStringToObject(cal, "ganzhi", ganzhi && ganzhi[0] ? ganzhi : ganzhi_buf);
        cJSON_AddStringToObject(cal, "festival", festival && festival[0] ? festival : festival_buf);
        cJSON_AddStringToObject(cal, "yi", yi ? yi : "");
        cJSON_AddStringToObject(cal, "ji", ji ? ji : "");
    }

    cJSON *list = add_widget(scene, "events", "list", 408, 112, 368, 344);
    if (list) {
        cJSON_AddStringToObject(list, "title", "今日");
        cJSON *arr = cJSON_AddArrayToObject(list, "items");
        cJSON *events = events_json ? cJSON_Parse(events_json) : NULL;
        if (cJSON_IsArray(events)) {
            const cJSON *e = NULL;
            cJSON_ArrayForEach(e, events) {
                if (cJSON_IsString(e)) {
                    cJSON *it = cJSON_CreateObject();
                    cJSON_AddStringToObject(it, "text", e->valuestring);
                    cJSON_AddStringToObject(it, "prio", "normal");
                    cJSON_AddItemToArray(arr, it);
                }
            }
        }
        cJSON_Delete(events);
        if (cJSON_GetArraySize(arr) == 0) {
            cJSON *it = cJSON_CreateObject();
            cJSON_AddStringToObject(it, "text", "今天没有日程");
            cJSON_AddStringToObject(it, "prio", "low");
            cJSON_AddItemToArray(arr, it);
        }
    }
    return apply_scene(result, page, scene, "calendar");
}

static const char *item_prio(const cJSON *item)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(item, "prio");
    return cJSON_IsString(p) ? p->valuestring : "normal";
}

static bool item_done(const cJSON *item)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "done"));
}

static int prio_rank(const char *prio)
{
    if (prio && strcmp(prio, "high") == 0) {
        return 0;
    }
    if (prio && strcmp(prio, "low") == 0) {
        return 2;
    }
    return 1;
}

static void sort_todo_items(cJSON *list)
{
    cJSON *items = cJSON_GetObjectItemCaseSensitive(list, "items");
    if (!cJSON_IsArray(items)) {
        return;
    }
    cJSON *sorted = cJSON_CreateArray();
    if (!sorted) {
        return;
    }
    /* high -> normal -> low, unfinished before finished */
    for (int rank = 0; rank <= 2; rank++) {
        for (int done = 0; done <= 1; done++) {
            const cJSON *it = NULL;
            cJSON_ArrayForEach(it, items) {
                if (prio_rank(item_prio(it)) != rank || (item_done(it) ? 1 : 0) != done) {
                    continue;
                }
                cJSON *copy = cJSON_Duplicate(it, true);
                if (copy) {
                    cJSON_AddItemToArray(sorted, copy);
                }
            }
        }
    }
    cJSON_ReplaceItemInObjectCaseSensitive(list, "items", sorted);
}

static esp_err_t display_todo_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *title = esp_mcp_property_list_get_property_string(properties, "title");
    const char *footer = esp_mcp_property_list_get_property_string(properties, "footer");
    const char *items_json = esp_mcp_property_list_get_property_array(properties, "items");

    cJSON *scene = new_scene();
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON *clock = add_widget(scene, "clock", "clock", 24, 16, 300, 72);
    if (clock) {
        cJSON_AddNumberToObject(clock, "size", 48);
        cJSON_AddStringToObject(clock, "format", "%H:%M");
    }
    cJSON *head = add_widget(scene, "title", "big_text", 340, 16, 436, 72);
    if (head) {
        cJSON_AddStringToObject(head, "text", title && title[0] ? title : "今天");
        cJSON_AddNumberToObject(head, "size", 40);
        cJSON_AddStringToObject(head, "align", "right");
    }
    cJSON *list = add_widget(scene, "todo", "list", 24, 104, 752, footer && footer[0] ? 312 : 352);
    if (list) {
        cJSON_AddStringToObject(list, "title", "");
        cJSON *arr = cJSON_AddArrayToObject(list, "items");
        cJSON *items = items_json ? cJSON_Parse(items_json) : NULL;
        if (cJSON_IsArray(items)) {
            const cJSON *it = NULL;
            cJSON_ArrayForEach(it, items) {
                if (!cJSON_IsObject(it)) {
                    continue;
                }
                const cJSON *text = cJSON_GetObjectItemCaseSensitive(it, "text");
                if (!cJSON_IsString(text) || !text->valuestring || !text->valuestring[0]) {
                    continue;
                }
                cJSON *copy = cJSON_CreateObject();
                cJSON_AddStringToObject(copy, "text", text->valuestring);
                const cJSON *prio = cJSON_GetObjectItemCaseSensitive(it, "prio");
                cJSON_AddStringToObject(copy, "prio",
                    cJSON_IsString(prio) ? prio->valuestring : "normal");
                const cJSON *done = cJSON_GetObjectItemCaseSensitive(it, "done");
                cJSON_AddBoolToObject(copy, "done", cJSON_IsTrue(done));
                const cJSON *due = cJSON_GetObjectItemCaseSensitive(it, "due");
                if (cJSON_IsString(due)) {
                    cJSON_AddStringToObject(copy, "due", due->valuestring);
                }
                cJSON_AddItemToArray(arr, copy);
            }
        }
        cJSON_Delete(items);
        if (cJSON_GetArraySize(arr) == 0) {
            cJSON *it = cJSON_CreateObject();
            cJSON_AddStringToObject(it, "text", "暂无待办");
            cJSON_AddStringToObject(it, "prio", "low");
            cJSON_AddItemToArray(arr, it);
        }
        sort_todo_items(list);
    }
    if (footer && footer[0]) {
        cJSON *f = add_widget(scene, "footer", "text", 24, 428, 752, 36);
        if (f) {
            cJSON_AddStringToObject(f, "text", footer);
            cJSON_AddNumberToObject(f, "size", 22);
            cJSON_AddStringToObject(f, "align", "left");
        }
    }
    return apply_scene(result, page, scene, "todo");
}

static esp_err_t display_quote_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *text = esp_mcp_property_list_get_property_string(properties, "text");
    const char *author = esp_mcp_property_list_get_property_string(properties, "author");
    const char *source = esp_mcp_property_list_get_property_string(properties, "source");
    int size = esp_mcp_property_list_get_property_int(properties, "size");
    if (!text || !text[0]) {
        return mcp_tool_reply_error(result, "invalid_args", "text is required");
    }
    if (size != 32 && size != 48 && size != 64) {
        size = 48;
    }

    cJSON *scene = new_scene();
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON *clock = add_widget(scene, "clock", "clock", 620, 16, 156, 64);
    if (clock) {
        cJSON_AddNumberToObject(clock, "size", 40);
        cJSON_AddStringToObject(clock, "format", "%H:%M");
        cJSON_AddStringToObject(clock, "align", "right");
    }
    cJSON *q = add_widget(scene, "quote", "quote", 48, 120, 704, 240);
    if (q) {
        cJSON_AddStringToObject(q, "text", text);
        cJSON_AddStringToObject(q, "author", author ? author : "");
        cJSON_AddStringToObject(q, "source", source ? source : "");
        cJSON_AddNumberToObject(q, "size", size);
        cJSON_AddStringToObject(q, "align", "center");
    }
    cJSON *line = add_widget(scene, "line", "divider", 200, 380, 400, 2);
    (void)line;
    return apply_scene(result, page, scene, "quote");
}

static esp_err_t display_horoscope_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *sign = esp_mcp_property_list_get_property_string(properties, "sign");
    const char *date = esp_mcp_property_list_get_property_string(properties, "date");
    const char *summary = esp_mcp_property_list_get_property_string(properties, "summary");
    const char *love = esp_mcp_property_list_get_property_string(properties, "love");
    const char *career = esp_mcp_property_list_get_property_string(properties, "career");
    const char *money = esp_mcp_property_list_get_property_string(properties, "money");
    const char *health = esp_mcp_property_list_get_property_string(properties, "health");
    const char *lucky_color = esp_mcp_property_list_get_property_string(properties, "lucky_color");
    const char *lucky_number = esp_mcp_property_list_get_property_string(properties, "lucky_number");
    int score = esp_mcp_property_list_get_property_int(properties, "score");

    cJSON *scene = new_scene();
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON *h = add_widget(scene, "horoscope", "horoscope", 24, 24, 752, 432);
    if (h) {
        cJSON_AddStringToObject(h, "sign", sign ? sign : "");
        cJSON_AddStringToObject(h, "date", date ? date : "");
        cJSON_AddStringToObject(h, "summary", summary ? summary : "");
        cJSON_AddStringToObject(h, "love", love ? love : "");
        cJSON_AddStringToObject(h, "career", career ? career : "");
        cJSON_AddStringToObject(h, "money", money ? money : "");
        cJSON_AddStringToObject(h, "health", health ? health : "");
        cJSON_AddStringToObject(h, "lucky_color", lucky_color ? lucky_color : "");
        cJSON_AddStringToObject(h, "lucky_number", lucky_number ? lucky_number : "");
        cJSON_AddNumberToObject(h, "score", score);
    }
    return apply_scene(result, page, scene, "horoscope");
}

static esp_err_t display_image_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int page = current_or_arg_page(properties);
    const char *url = esp_mcp_property_list_get_property_string(properties, "url");
    const char *fit = esp_mcp_property_list_get_property_string(properties, "fit");
    if (!url || !url[0]) {
        return mcp_tool_reply_error(result, "invalid_args", "url is required");
    }
    const char *prefix = CONFIG_APP_DISPLAY_IMAGE_URL_PREFIX;
    if (!prefix[0]) {
        return mcp_tool_reply_error(result, "not_configured",
                                    "image URL prefix is not configured");
    }
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        return mcp_tool_reply_error(result, "url_not_allowed", "image URL is not allowlisted");
    }

    cJSON *scene = new_scene();
    if (!scene) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    const board_desc_t *board = board_get();
    int width = board && board->width > 0 ? board->width : 800;
    int height = board && board->height > 0 ? board->height : 480;
    cJSON *img = add_widget(scene, "image", "image", 0, 0, width, height);
    if (img) {
        cJSON_AddStringToObject(img, "url", url);
        cJSON_AddStringToObject(img, "fit", fit ? fit : "cover");
    }
    return apply_scene(result, page, scene, "image");
}

static void add_page_prop(esp_mcp_tool_t *tool)
{
    esp_mcp_tool_add_property(tool,
        esp_mcp_property_create_with_int_and_range("page", -1, -1, CONFIG_APP_DISPLAY_MAX_PAGES - 1));
}

void mcp_register_display_tools(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }

    esp_mcp_tool_t *tool = esp_mcp_tool_create_ex("display_info", "Display Info",
        "Get display, UI pages, clock, lunar date and board information.", display_info_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_render", "Render Scene",
        "Render a scene JSON on a page. Scene: {v:1,theme,widgets:[{id,type,x,y,w,h,...}]}. "
        "Types: text,big_text,clock,calendar,list,quote,horoscope,image,qr,divider,progress.",
        display_render_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_object("scene", "{}"));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("scene_json", ""));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_patch", "Patch Widget",
        "Update one widget by id on a page. Args: page, widget (object with id and fields to change).",
        display_patch_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_object("widget", "{}"));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_page", "Switch Page",
        "Switch page: action=next|prev, or set page (default current).", display_page_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("action", "set"));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_clear", "Clear Page",
        "Clear a page (default current).", display_clear_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_notify", "Show Notification",
        "Show a temporary toast/overlay. Args: text, level(info|success|warn|error), duration_ms.",
        display_notify_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("text", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("level", "info"));
        esp_mcp_tool_add_property(tool,
            esp_mcp_property_create_with_int_and_range("duration_ms", 3000, 500, 60000));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":false,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_brightness", "Set Brightness",
        "Set software brightness 0-100. 0 turns the backlight off.", display_brightness_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool,
            esp_mcp_property_create_with_int_and_range("percent", 70, 0, 100));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_calendar", "Show Calendar",
        "Render the calendar template. Optional: solar, weekday, lunar, ganzhi, festival, yi, ji, "
        "events (array of strings), page.", display_calendar_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("solar", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("weekday", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("lunar", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("ganzhi", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("festival", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("yi", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("ji", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_array("events", "[]"));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_todo", "Show Todo List",
        "Render the todo template. Args: title, items (array of {text,prio,done,due}), footer, page.",
        display_todo_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("title", "今天"));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("footer", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_array("items", "[]"));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_quote", "Show Quote",
        "Render a large quote. Args: text, author, source, size(32|48|64), page.", display_quote_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("text", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("author", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("source", ""));
        esp_mcp_tool_add_property(tool,
            esp_mcp_property_create_with_int_and_range("size", 48, 32, 64));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_horoscope", "Show Horoscope",
        "Render a horoscope card. Args: sign, date, summary, love, career, money, health, "
        "lucky_color, lucky_number, score(0-5), page.", display_horoscope_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("sign", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("date", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("summary", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("love", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("career", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("money", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("health", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("lucky_color", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("lucky_number", ""));
        esp_mcp_tool_add_property(tool,
            esp_mcp_property_create_with_int_and_range("score", 3, 0, 5));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("display_image", "Show Image",
        "Show an image from an allowlisted LAN URL. Args: url, fit(cover|contain), page.",
        display_image_cb);
    if (tool) {
        add_page_prop(tool);
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("url", ""));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("fit", "cover"));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":true}");
        esp_mcp_add_tool(mcp, tool);
    }
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */
