#include "provisioning_ui.h"

#include "sdkconfig.h"

#if CONFIG_APP_DISPLAY_BACKEND_LVGL

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "provisioning_ui.h"
#include "ui_lvgl.h"
#include "ui_service.h"

static const char *TAG = "prov_ui";

#define COL_BG      0x0B0F14
#define COL_CARD    0x161B22
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E
#define COL_ACCENT  0x58A6FF
#define COL_HIGH    0xF85149

static const provisioning_ui_backend_t *s_backend;
static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_done_btn;
static lv_obj_t *s_pwd_screen;
static lv_obj_t *s_pwd_ta;
static char s_pwd_ssid[33];
static lv_timer_t *s_timer;
static provisioning_scan_item_t s_items[CONFIG_APP_WIFI_SCAN_MAX];
static bool s_visible;
static bool s_was_busy;

static void style_button(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(btn, ui_lvgl_cjk_font(), 0);
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    style_button(btn);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *make_status(lv_obj_t *parent, int x, int y, int w, int h, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_line_space(l, 4, 0);
    return l;
}

static void pwd_connect(void)
{
    if (!s_backend || !s_pwd_ta) {
        return;
    }
    const char *pwd = lv_textarea_get_text(s_pwd_ta);
    esp_err_t err = s_backend->connect(s_pwd_ssid, pwd);
    if (s_status) {
        if (err == ESP_OK) {
            lv_label_set_text_fmt(s_status, "正在连接 %s …", s_pwd_ssid);
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_ACCENT), 0);
        } else {
            lv_label_set_text_fmt(s_status, "连接失败：%s", esp_err_to_name(err));
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_HIGH), 0);
        }
    }
    if (s_pwd_screen) {
        lv_obj_del_async(s_pwd_screen);
        s_pwd_screen = NULL;
        s_pwd_ta = NULL;
    }
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

static void pwd_connect_cb(lv_event_t *e)
{
    (void)e;
    pwd_connect();
}

static void pwd_ready_cb(lv_event_t *e)
{
    (void)e;
    pwd_connect();
}

static void pwd_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_pwd_screen) {
        lv_obj_del_async(s_pwd_screen);
        s_pwd_screen = NULL;
        s_pwd_ta = NULL;
    }
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

static void pwd_show_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    if (s_pwd_ta) {
        lv_textarea_set_password_mode(s_pwd_ta, !lv_obj_has_state(cb, LV_STATE_CHECKED));
    }
}

static void open_password(const char *ssid)
{
    if (!ui_lvgl_lock(1000)) {
        return;
    }
    snprintf(s_pwd_ssid, sizeof(s_pwd_ssid), "%s", ssid ? ssid : "");

    if (s_pwd_screen) {
        lv_obj_del_async(s_pwd_screen);
        s_pwd_screen = NULL;
    }
    s_pwd_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_pwd_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_pwd_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_pwd_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = make_button(s_pwd_screen, 24, 8, 120, 36, "取消");
    lv_obj_add_event_cb(cancel, pwd_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect = make_button(s_pwd_screen, 636, 8, 140, 36, "连接");
    lv_obj_set_style_bg_color(connect, lv_color_hex(COL_ACCENT), 0);
    lv_obj_add_event_cb(connect, pwd_connect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = make_status(s_pwd_screen, 156, 8, 464, 36, s_pwd_ssid);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    s_pwd_ta = lv_textarea_create(s_pwd_screen);
    lv_obj_set_pos(s_pwd_ta, 24, 52);
    lv_obj_set_size(s_pwd_ta, 752, 48);
    lv_textarea_set_one_line(s_pwd_ta, true);
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_textarea_set_placeholder_text(s_pwd_ta, "Wi-Fi 密码");
    lv_obj_set_style_text_font(s_pwd_ta, ui_lvgl_cjk_font(), 0);

    lv_obj_t *cb = lv_checkbox_create(s_pwd_screen);
    lv_obj_set_pos(cb, 24, 106);
    lv_checkbox_set_text(cb, "显示密码");
    lv_obj_set_style_text_font(cb, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_color(cb, lv_color_hex(COL_MUTED), 0);
    lv_obj_add_event_cb(cb, pwd_show_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *kb = lv_keyboard_create(s_pwd_screen);
    /* lv_keyboard aligns itself to BOTTOM_MID in its constructor and
     * lv_obj_set_pos() only sets X/Y, so the ALIGN style must be cleared or
     * every later size change re-applies bottom alignment and pushes the
     * keyboard off the screen. */
    lv_obj_set_style_align(kb, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_pos(kb, 8, 136);
    lv_obj_set_size(kb, 784, 330);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 1, 0);
    lv_keyboard_set_textarea(kb, s_pwd_ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, pwd_ready_cb, LV_EVENT_READY, NULL);

    lv_screen_load(s_pwd_screen);
    ui_lvgl_unlock();
}

static void ssid_cb(lv_event_t *e)
{
    provisioning_scan_item_t *item = lv_event_get_user_data(e);
    if (item) {
        open_password(item->ssid);
    }
}

static void rebuild_list(int count)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    if (count <= 0) {
        lv_obj_t *l = make_status(s_list, 8, 8, 720, 40, "没有找到 Wi-Fi，请点“重新扫描”");
        (void)l;
        return;
    }
    int max = count < CONFIG_APP_WIFI_SCAN_MAX ? count : CONFIG_APP_WIFI_SCAN_MAX;
    for (int i = 0; i < max; i++) {
        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 48);
        style_button(btn);
        lv_obj_set_style_pad_all(btn, 10, 0);

        lv_obj_t *name = lv_label_create(btn);
        lv_label_set_text(name, s_items[i].ssid[0] ? s_items[i].ssid : "(隐藏网络)");
        lv_obj_set_style_text_font(name, ui_lvgl_cjk_font(), 0);
        lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);

        char meta[32];
        snprintf(meta, sizeof(meta), "%d dBm", s_items[i].rssi);
        lv_obj_t *rssi = lv_label_create(btn);
        lv_label_set_text(rssi, meta);
        lv_obj_set_style_text_font(rssi, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(rssi, lv_color_hex(COL_MUTED), 0);
        lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, -8, 0);

        lv_obj_add_event_cb(btn, ssid_cb, LV_EVENT_CLICKED, &s_items[i]);
    }
}

static void rescan_cb(lv_event_t *e)
{
    (void)e;
    if (!s_backend) {
        return;
    }
    s_was_busy = true;
    if (s_status) {
        lv_label_set_text(s_status, "正在扫描 Wi-Fi …");
        lv_obj_set_style_text_color(s_status, lv_color_hex(COL_ACCENT), 0);
    }
    s_backend->scan_start();
}

static void softap_cb(lv_event_t *e)
{
    (void)e;
    if (!s_backend) {
        return;
    }
    s_backend->start_softap();
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    if (s_backend) {
        s_backend->forget();
    }
}

static void done_cb(lv_event_t *e)
{
    (void)e;
    provisioning_ui_hide();
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_backend) {
        return;
    }

    if (s_backend->scan_busy()) {
        s_was_busy = true;
        if (s_status) {
            lv_label_set_text(s_status, "正在扫描 Wi-Fi …");
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_ACCENT), 0);
        }
    } else if (s_was_busy) {
        int n = s_backend->scan_get(s_items, CONFIG_APP_WIFI_SCAN_MAX);
        rebuild_list(n);
        s_was_busy = false;
    }

    provisioning_status_t st = {0};
    s_backend->get_status(&st);
    bool connected = strcmp(st.state, "connected") == 0;
    if (s_done_btn) {
        if (connected) {
            lv_obj_remove_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_status) {
        if (connected) {
            lv_label_set_text_fmt(s_status, "已连接 %s\nIP: %s\nToken: %s",
                                  st.ssid, st.ip, st.token);
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_ACCENT), 0);
        } else if (strcmp(st.state, "connecting") == 0) {
            lv_label_set_text_fmt(s_status, "正在连接 %s …", st.ssid);
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_ACCENT), 0);
        } else if (strcmp(st.state, "error") == 0) {
            lv_label_set_text_fmt(s_status, "连接失败：%s\n请重新扫描或选择其他网络",
                                  st.error[0] ? st.error : "unknown");
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_HIGH), 0);
        } else if (st.ap_active) {
            lv_label_set_text_fmt(s_status,
                                  "手机配网已开启\nAP: %s\n密码: %s\nhttp://192.168.4.1",
                                  st.ap_ssid, st.ap_password);
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_MUTED), 0);
        } else {
            lv_label_set_text(s_status, "请选择要连接的 Wi-Fi");
            lv_obj_set_style_text_color(s_status, lv_color_hex(COL_MUTED), 0);
        }
    }
}

esp_err_t provisioning_ui_init(const provisioning_ui_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    s_backend = backend;
    ESP_LOGI(TAG, "provisioning UI ready");
    return ESP_OK;
}

void provisioning_ui_show(void)
{
    if (s_visible || !s_backend) {
        return;
    }
    if (!ui_lvgl_lock(2000)) {
        ESP_LOGW(TAG, "show: LVGL lock timeout");
        return;
    }
    ESP_LOGI(TAG, "showing provisioning screen");

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_status(s_screen, 24, 18, 752, 42, "连接 Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, ui_lvgl_cjk_font(), 0);

    s_status = make_status(s_screen, 24, 58, 752, 96, "请选择要连接的 Wi-Fi");
    lv_obj_set_style_text_color(s_status, lv_color_hex(COL_MUTED), 0);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_pos(s_list, 24, 164);
    lv_obj_set_size(s_list, 752, 228);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *rescan = make_button(s_screen, 24, 408, 140, 44, "重新扫描");
    lv_obj_add_event_cb(rescan, rescan_cb, LV_EVENT_CLICKED, NULL);
#if CONFIG_APP_PROVISIONING_ALLOW_SOFTAP_FALLBACK
    lv_obj_t *softap = make_button(s_screen, 180, 408, 140, 44, "手机配网");
    lv_obj_add_event_cb(softap, softap_cb, LV_EVENT_CLICKED, NULL);
#endif
    lv_obj_t *forget = make_button(s_screen, 336, 408, 140, 44, "清除配置");
    lv_obj_set_style_bg_color(forget, lv_color_hex(0x5A1E1E), 0);
    lv_obj_add_event_cb(forget, forget_cb, LV_EVENT_CLICKED, NULL);

    s_done_btn = make_button(s_screen, 636, 408, 140, 44, "进入桌面");
    lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_done_btn, done_cb, LV_EVENT_CLICKED, NULL);

    lv_screen_load(s_screen);
    s_visible = true;
    s_was_busy = true;
    s_backend->scan_start();
    s_timer = lv_timer_create(timer_cb, 500, NULL);
    ui_lvgl_unlock();
}

void provisioning_ui_hide(void)
{
    if (!s_visible) {
        return;
    }
    if (!ui_lvgl_lock(2000)) {
        return;
    }
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_pwd_screen) {
        lv_obj_del_async(s_pwd_screen);
        s_pwd_screen = NULL;
        s_pwd_ta = NULL;
    }
    if (s_screen) {
        lv_obj_del_async(s_screen);
        s_screen = NULL;
        s_list = NULL;
        s_status = NULL;
        s_done_btn = NULL;
    }
    s_visible = false;
    ui_lvgl_unlock();
    ui_lvgl_request_scene_refresh();
}

bool provisioning_ui_is_visible(void)
{
    return s_visible;
}

#endif /* CONFIG_APP_DISPLAY_BACKEND_LVGL */
