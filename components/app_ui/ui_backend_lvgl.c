#include "ui_backend.h"

#include "sdkconfig.h"

#if CONFIG_APP_DISPLAY_BACKEND_LVGL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "provisioning_ui.h"
#include "ui_lvgl.h"
#include "ui_service.h"

static const char *TAG = "ui_lvgl";

#define COL_BG      0x0B0F14
#define COL_CARD    0x161B22
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E
#define COL_ACCENT  0x58A6FF
#define COL_HIGH    0xF85149
#define COL_NORMAL  0xD29922
#define COL_LOW     0x8B949E

static lv_display_t *s_disp;
static lv_obj_t *s_scene_screen;
static lv_obj_t *s_dim_overlay;
static lv_timer_t *s_clock_timer;
static lv_obj_t *s_clock_label;
static char s_clock_fmt[16];
static int s_brightness = 70;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;
static lv_font_t *s_cjk24;
static void *s_cjk24_buf;

bool ui_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void ui_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

lv_display_t *ui_lvgl_display(void)
{
    return s_disp;
}

const lv_font_t *ui_lvgl_cjk_font(void)
{
    return s_cjk24 ? s_cjk24 : &lv_font_source_han_sans_sc_16_cjk;
}

static void load_fonts(void)
{
    FILE *f = fopen("/spiffs/fonts/ui24.bin", "rb");
    if (!f) {
        ESP_LOGW(TAG, "font /spiffs/fonts/ui24.bin not found, using built-in CJK 16");
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return;
    }
    rewind(f);
    s_cjk24_buf = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_cjk24_buf) {
        s_cjk24_buf = malloc((size_t)size);
    }
    if (!s_cjk24_buf) {
        fclose(f);
        ESP_LOGW(TAG, "not enough memory for font (%ld bytes)", size);
        return;
    }
    size_t n = fread(s_cjk24_buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(s_cjk24_buf);
        s_cjk24_buf = NULL;
        return;
    }
    s_cjk24 = lv_binfont_create_from_buffer(s_cjk24_buf, (uint32_t)size);
    if (!s_cjk24) {
        ESP_LOGW(TAG, "binfont parse failed");
        free(s_cjk24_buf);
        s_cjk24_buf = NULL;
        return;
    }
    s_cjk24->fallback = &lv_font_source_han_sans_sc_16_cjk;
    ESP_LOGI(TAG, "loaded CJK font, %ld bytes", size);
}

void ui_lvgl_request_scene_refresh(void)
{
    ui_service_refresh();
}

static const char *jstr(const cJSON *obj, const char *key, const char *def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}

static int jint(const cJSON *obj, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static bool jbool(const cJSON *obj, const char *key, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) {
        return cJSON_IsTrue(v);
    }
    return def;
}

static bool is_ascii(const char *s)
{
    if (!s) {
        return true;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p > 127) {
            return false;
        }
    }
    return true;
}

static const lv_font_t *font_for(const char *text, int size)
{
    if (!is_ascii(text)) {
        return ui_lvgl_cjk_font();
    }
    if (size >= 44) {
        return &lv_font_montserrat_48;
    }
    if (size >= 26) {
        return &lv_font_montserrat_28;
    }
    if (size >= 22) {
        return &lv_font_montserrat_24;
    }
    return &lv_font_montserrat_20;
}

static lv_text_align_t align_from(const char *s)
{
    if (s && strcmp(s, "center") == 0) {
        return LV_TEXT_ALIGN_CENTER;
    }
    if (s && strcmp(s, "right") == 0) {
        return LV_TEXT_ALIGN_RIGHT;
    }
    return LV_TEXT_ALIGN_LEFT;
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h,
                            const char *text, const lv_font_t *font,
                            uint32_t color, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    if (!l) {
        return NULL;
    }
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_style_text_line_space(l, 4, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text ? text : "");
    return l;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    if (!c) {
        return NULL;
    }
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static uint32_t prio_color(const char *prio)
{
    if (prio && strcmp(prio, "high") == 0) {
        return COL_HIGH;
    }
    if (prio && strcmp(prio, "low") == 0) {
        return COL_LOW;
    }
    return COL_NORMAL;
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_clock_label) {
        return;
    }
    time_t now = time(NULL);
    struct tm local = {0};
    if (!localtime_r(&now, &local)) {
        return;
    }
    char buf[32] = {0};
    strftime(buf, sizeof(buf), s_clock_fmt[0] ? s_clock_fmt : "%H:%M", &local);
    lv_label_set_text(s_clock_label, buf);
}

static void render_clock(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    int size = jint(w, "size", 64);
    s_clock_label = make_label(parent, x, y, ww, hh, "--:--", font_for("00:00", size),
                               COL_TEXT, align_from(jstr(w, "align", "left")));
    snprintf(s_clock_fmt, sizeof(s_clock_fmt), "%s", jstr(w, "format", "%H:%M"));
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
    }
    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    clock_timer_cb(NULL);
}

static void render_text(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    const char *text = jstr(w, "text", "");
    int size = jint(w, "size", 28);
    make_label(parent, x, y, ww, hh, text, font_for(text, size), COL_TEXT,
               align_from(jstr(w, "align", "left")));
}

static void render_calendar(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    lv_obj_t *card = make_card(parent, x, y, ww, hh);
    if (!card) {
        return;
    }
    int cw = ww - 24;
    int ch = hh - 24;
    const char *solar = jstr(w, "solar", "--");
    const char *weekday = jstr(w, "weekday", "");
    const char *lunar = jstr(w, "lunar", "");
    const char *ganzhi = jstr(w, "ganzhi", "");
    const char *festival = jstr(w, "festival", "");

    make_label(card, 0, 0, cw, ch * 45 / 100, solar, font_for(solar, 48), COL_TEXT,
               LV_TEXT_ALIGN_LEFT);

    char line[96];
    if (weekday[0] || lunar[0]) {
        snprintf(line, sizeof(line), "%s  %s", weekday, lunar);
        make_label(card, 0, ch * 48 / 100, cw, ch * 18 / 100, line,
                   ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_LEFT);
    }
    if (ganzhi[0]) {
        make_label(card, 0, ch * 66 / 100, cw, ch * 14 / 100, ganzhi,
                   ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_LEFT);
    }
    if (festival[0]) {
        make_label(card, 0, ch * 80 / 100, cw, ch * 14 / 100, festival,
                   ui_lvgl_cjk_font(), COL_ACCENT, LV_TEXT_ALIGN_LEFT);
    }
    const char *yi = jstr(w, "yi", "");
    const char *ji = jstr(w, "ji", "");
    if (yi[0] || ji[0]) {
        snprintf(line, sizeof(line), "宜:%s  忌:%s", yi, ji);
        make_label(card, 0, ch * 92 / 100, cw, ch * 12 / 100, line,
                   ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_LEFT);
    }
}

static void render_list(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    lv_obj_t *card = make_card(parent, x, y, ww, hh);
    if (!card) {
        return;
    }
    int cw = ww - 24;
    int ch = hh - 24;
    const char *title = jstr(w, "title", "");
    int top = 0;
    if (title[0]) {
        make_label(card, 0, 0, cw, 32, title,
                   ui_lvgl_cjk_font(), COL_ACCENT, LV_TEXT_ALIGN_LEFT);
        top = 38;
    }
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_pos(list, 0, top);
    lv_obj_set_size(list, cw, ch - top);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(w, "items");
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {
        const char *text = jstr(it, "text", "");
        if (!text[0]) {
            continue;
        }
        bool done = jbool(it, "done", false);
        const char *prio = jstr(it, "prio", "normal");
        const char *due = jstr(it, "due", "");

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 44);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_set_pos(bar, 0, 8);
        lv_obj_set_size(bar, 4, 28);
        lv_obj_set_style_bg_color(bar, lv_color_hex(prio_color(prio)), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);

        int text_w = due[0] ? cw - 96 : cw - 20;
        lv_obj_t *label = make_label(row, 14, 4, text_w, 36, text,
                                     ui_lvgl_cjk_font(),
                                     done ? COL_MUTED : COL_TEXT, LV_TEXT_ALIGN_LEFT);
        if (done) {
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        }
        if (due[0]) {
            make_label(row, cw - 82, 4, 82, 36, due,
                       ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_RIGHT);
        }
    }
    if (!items || cJSON_GetArraySize(items) == 0) {
        make_label(list, 0, 8, cw, 40, "暂无内容",
                   ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_LEFT);
    }
}

static void render_quote(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    lv_obj_t *card = make_card(parent, x, y, ww, hh);
    if (!card) {
        return;
    }
    int cw = ww - 24;
    int ch = hh - 24;
    const char *text = jstr(w, "text", "");
    int size = jint(w, "size", 48);
    make_label(card, 0, 0, cw, ch * 70 / 100, text, font_for(text, size),
               COL_TEXT, LV_TEXT_ALIGN_CENTER);
    char author[96];
    const char *a = jstr(w, "author", "");
    const char *src = jstr(w, "source", "");
    if (a[0] || src[0]) {
        if (a[0] && src[0]) {
            snprintf(author, sizeof(author), "—— %s《%s》", a, src);
        } else if (a[0]) {
            snprintf(author, sizeof(author), "—— %s", a);
        } else {
            snprintf(author, sizeof(author), "—— %s", src);
        }
        make_label(card, 0, ch * 76 / 100, cw, ch * 20 / 100, author,
                   ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_RIGHT);
    }
}

static void render_horoscope(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    lv_obj_t *card = make_card(parent, x, y, ww, hh);
    if (!card) {
        return;
    }
    int cw = ww - 24;
    int ch = hh - 24;
    const char *sign = jstr(w, "sign", "");
    const char *date = jstr(w, "date", "");
    char head[96];
    snprintf(head, sizeof(head), "%s  %s", sign, date);
    make_label(card, 0, 0, cw, 40, head,
               ui_lvgl_cjk_font(), COL_ACCENT, LV_TEXT_ALIGN_LEFT);
    make_label(card, 0, 44, cw, ch * 30 / 100, jstr(w, "summary", ""),
               ui_lvgl_cjk_font(), COL_TEXT, LV_TEXT_ALIGN_LEFT);

    const char *labels[] = {"爱情", "事业", "财运", "健康"};
    const char *keys[] = {"love", "career", "money", "health"};
    for (int i = 0; i < 4; i++) {
        char line[128];
        snprintf(line, sizeof(line), "%s  %s", labels[i], jstr(w, keys[i], ""));
        make_label(card, 0, ch * (40 + i * 11) / 100, cw, ch * 10 / 100, line,
                   ui_lvgl_cjk_font(), COL_TEXT, LV_TEXT_ALIGN_LEFT);
    }
    char lucky[96];
    snprintf(lucky, sizeof(lucky), "幸运色 %s   幸运数字 %s",
             jstr(w, "lucky_color", "-"), jstr(w, "lucky_number", "-"));
    make_label(card, 0, ch * 88 / 100, cw, ch * 10 / 100, lucky,
               ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_LEFT);
}

static void render_image(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    lv_obj_t *card = make_card(parent, x, y, ww, hh);
    if (!card) {
        return;
    }
    const char *url = jstr(w, "url", "");
    make_label(card, 0, 0, ww - 24, hh - 24,
               url[0] ? "[图片待接入]\nURL 已保存" : "[图片]",
               ui_lvgl_cjk_font(), COL_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void render_progress(lv_obj_t *parent, const cJSON *w, int x, int y, int ww, int hh)
{
    int value = jint(w, "value", 0);
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, ww, hh);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
}

static void settings_btn_cb(lv_event_t *e)
{
    (void)e;
    provisioning_ui_show();
}

static void add_settings_button(lv_obj_t *screen, int width, int height)
{
    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 52, 40);
    lv_obj_set_pos(btn, width - 64, height - 52);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "设置");
    lv_obj_set_style_text_font(label, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COL_MUTED), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);
}

static void render_widget(lv_obj_t *screen, const cJSON *w)
{
    const char *type = jstr(w, "type", "");
    int x = jint(w, "x", 0);
    int y = jint(w, "y", 0);
    int ww = jint(w, "w", 100);
    int hh = jint(w, "h", 40);

    if (strcmp(type, "clock") == 0) {
        render_clock(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "text") == 0 || strcmp(type, "big_text") == 0) {
        render_text(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "calendar") == 0) {
        render_calendar(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "list") == 0) {
        render_list(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "quote") == 0) {
        render_quote(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "horoscope") == 0) {
        render_horoscope(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "image") == 0) {
        render_image(screen, w, x, y, ww, hh);
    } else if (strcmp(type, "qr") == 0) {
        make_label(screen, x, y, ww, hh, "[二维码]", ui_lvgl_cjk_font(),
                   COL_MUTED, LV_TEXT_ALIGN_CENTER);
    } else if (strcmp(type, "divider") == 0) {
        lv_obj_t *line = lv_obj_create(screen);
        lv_obj_set_pos(line, x, y);
        lv_obj_set_size(line, ww, hh);
        lv_obj_set_style_bg_color(line, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_radius(line, 1, 0);
    } else if (strcmp(type, "progress") == 0) {
        render_progress(screen, w, x, y, ww, hh);
    }
}

static esp_err_t lcd_init(void)
{
    const board_desc_t *b = board_get();
    if (!b || !b->has_display) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dma_burst_size = 64,
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .de_gpio_num = b->lcd_de,
        .pclk_gpio_num = b->lcd_pclk,
        .vsync_gpio_num = b->lcd_vsync,
        .hsync_gpio_num = b->lcd_hsync,
        .disp_gpio_num = -1,
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = (uint32_t)b->width,
            .v_res = (uint32_t)b->height,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .flags = {
            .fb_in_psram = 1,
        },
        .num_fbs = 2,
        .bounce_buffer_size_px = (size_t)b->width * 10,
    };
    for (int i = 0; i < 16; i++) {
        cfg.data_gpio_nums[i] = b->lcd_data[i];
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel start failed: %s", esp_err_to_name(err));
        return err;
    }

    const lvgl_port_cfg_t port_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .timer_period_ms = 5,
    };
    err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = (uint32_t)b->width * b->height,
        .double_buffer = true,
        .hres = (uint32_t)b->width,
        .vres = (uint32_t)b->height,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .full_refresh = false,
            .direct_mode = true,
            .swap_bytes = false,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "add RGB display failed");
        return ESP_FAIL;
    }

    board_backlight_set(true);

    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus missing");
        return ESP_ERR_INVALID_STATE;
    }
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    err = esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch IO init failed: %s", esp_err_to_name(err));
        return ESP_OK; /* display still usable */
    }
    esp_lcd_touch_handle_t touch = NULL;
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = (uint16_t)b->width,
        .y_max = (uint16_t)b->height,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 init failed: %s", esp_err_to_name(err));
        return ESP_OK;
    }
    const lvgl_port_touch_cfg_t touch_port_cfg = {
        .disp = s_disp,
        .handle = touch,
        .scale = {1.0f, 1.0f},
    };
    lvgl_port_add_touch(&touch_port_cfg);

    ESP_LOGI(TAG, "RGB+LVGL ready: %dx%d, heap internal=%u KB psram=%u KB",
             b->width, b->height,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return ESP_OK;
}

static void dim_update(void)
{
    if (!s_dim_overlay) {
        s_dim_overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_dim_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(s_dim_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_dim_overlay, lv_color_black(), 0);
        lv_obj_set_style_border_width(s_dim_overlay, 0, 0);
        lv_obj_set_style_radius(s_dim_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_dim_overlay, 0, 0);
        lv_obj_remove_flag(s_dim_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_dim_overlay, LV_OBJ_FLAG_SCROLLABLE);
    }
    int opa = (100 - s_brightness) * 2;
    if (opa > 220) {
        opa = 220;
    }
    lv_obj_set_style_bg_opa(s_dim_overlay, (lv_opa_t)opa, 0);
    lv_obj_move_foreground(s_dim_overlay);
}

static esp_err_t lvgl_init(void)
{
    esp_err_t err = lcd_init();
    if (err != ESP_OK) {
        return err;
    }
    load_fonts();
    if (ui_lvgl_lock(2000)) {
        dim_update();
        ui_lvgl_unlock();
    }
    return ESP_OK;
}

static esp_err_t lvgl_apply_scene(int page, const char *scene_json)
{
    (void)page;
    if (!scene_json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_lvgl_lock(3000)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    s_clock_label = NULL;
    if (s_scene_screen) {
        lv_obj_del(s_scene_screen);
        s_scene_screen = NULL;
    }

    s_scene_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scene_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_scene_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scene_screen, LV_OBJ_FLAG_SCROLLABLE);

    cJSON *root = cJSON_Parse(scene_json);
    if (!root) {
        make_label(s_scene_screen, 20, 20, 760, 60, "场景 JSON 解析失败",
                   ui_lvgl_cjk_font(), COL_HIGH, LV_TEXT_ALIGN_LEFT);
    } else {
        const cJSON *widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        const cJSON *w = NULL;
        cJSON_ArrayForEach(w, widgets) {
            if (cJSON_IsObject(w)) {
                render_widget(s_scene_screen, w);
            }
        }
        cJSON_Delete(root);
    }

    const board_desc_t *b = board_get();
    add_settings_button(s_scene_screen, b ? b->width : 800, b ? b->height : 480);

    if (!provisioning_ui_is_visible()) {
        lv_screen_load(s_scene_screen);
    }
    dim_update();
    ui_lvgl_unlock();
    return ESP_OK;
}

static esp_err_t lvgl_set_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    board_set_brightness((uint8_t)percent);
    if (ui_lvgl_lock(500)) {
        dim_update();
        ui_lvgl_unlock();
    }
    return ESP_OK;
}

static esp_err_t lvgl_set_backlight(bool on)
{
    return board_backlight_set(on);
}

static void toast_timer_cb(lv_timer_t *timer)
{
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
    s_toast_timer = NULL;
    lv_timer_del(timer);
}

static esp_err_t lvgl_notify(const char *text, const char *level, int duration_ms)
{
    (void)level;
    if (!text || !text[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_lvgl_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 10, 0);
    lv_obj_set_style_border_width(s_toast, 0, 0);
    lv_obj_set_style_pad_all(s_toast, 12, 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = lv_label_create(s_toast);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_lvgl_cjk_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(label);
    s_toast_timer = lv_timer_create(toast_timer_cb, duration_ms > 0 ? duration_ms : 3000, NULL);
    ui_lvgl_unlock();
    return ESP_OK;
}

const ui_backend_t ui_backend_lvgl = {
    .name = "lvgl",
    .init = lvgl_init,
    .apply_scene = lvgl_apply_scene,
    .set_brightness = lvgl_set_brightness,
    .set_backlight = lvgl_set_backlight,
    .notify = lvgl_notify,
};

#endif /* CONFIG_APP_DISPLAY_BACKEND_LVGL */
