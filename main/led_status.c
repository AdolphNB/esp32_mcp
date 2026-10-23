#include "led_status.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static bool parse_color(const char *text, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!text || !r || !g || !b) {
        return false;
    }
    if (*text == '#') {
        text++;
    }
    if (strlen(text) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }
    unsigned long value = strtoul(text, NULL, 16);
    *r = (uint8_t)((value >> 16) & 0xff);
    *g = (uint8_t)((value >> 8) & 0xff);
    *b = (uint8_t)(value & 0xff);
    return true;
}

esp_err_t led_status_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = APP_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS2812 init failed: %s", esp_err_to_name(err));
        s_ready = false;
        return err;
    }
    s_ready = true;
    led_strip_clear(s_strip);
    return ESP_OK;
}

esp_err_t led_status_set(bool on, const char *color, int brightness)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (brightness < 0) {
        brightness = 0;
    }
    if (brightness > 100) {
        brightness = 100;
    }

    uint8_t r = 0, g = 0, b = 255;
    if (color && color[0] && !parse_color(color, &r, &g, &b)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rs = (uint8_t)((r * brightness) / 100);
    uint8_t gs = (uint8_t)((g * brightness) / 100);
    uint8_t bs = (uint8_t)((b * brightness) / 100);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err;
    if (on) {
        err = led_strip_set_pixel(s_strip, 0, rs, gs, bs);
        if (err == ESP_OK) {
            err = led_strip_refresh(s_strip);
        }
    } else {
        err = led_strip_clear(s_strip);
    }
    xSemaphoreGive(s_lock);
    return err;
}
