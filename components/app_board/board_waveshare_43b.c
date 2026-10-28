#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io_expander_ch422g.h"
#include "rtc_pcf85063.h"

static const char *TAG = "board_43b";

esp_err_t board_waveshare_43b_lcd_reset(void);
esp_err_t board_waveshare_43b_touch_reset(void);
bool board_waveshare_43b_rtc_present(void);

static i2c_master_bus_handle_t s_bus;
static ch422g_t *s_ch422g;
static rtc_pcf85063_t *s_rtc;
static uint8_t s_brightness;

const board_desc_t board_waveshare_43b_desc = {
    .id = "waveshare_43b",
    .name = "Waveshare ESP32-S3-Touch-LCD-4.3B",
    .width = 800,
    .height = 480,
    .has_display = true,
    .has_touch = true,
    .has_rtc = true,
    .has_io_expander = true,
    .has_isolated_io = true,
    .has_rs485 = true,
    .has_can = true,
    .has_backlight_pwm = false,
    .has_gpio_header = false,
    .i2c_sda = 8,
    .i2c_scl = 9,
    .lcd_vsync = 3,
    .lcd_hsync = 46,
    .lcd_de = 5,
    .lcd_pclk = 7,
    .lcd_data = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
    .touch_int = 4,
    .rtc_int = 6,
    .rs485_tx = 44,
    .rs485_rx = 43,
    .can_tx = 15,
    .can_rx = 16,
    .led_gpio = -1,
};

static esp_err_t set_io_bit(uint8_t bit, bool level)
{
    return ch422g_write_io(s_ch422g, level ? bit : 0, bit);
}

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = board_waveshare_43b_desc.i2c_sda,
        .scl_io_num = board_waveshare_43b_desc.i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t board_waveshare_43b_init(i2c_master_bus_handle_t *bus_out)
{
    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        return err;
    }
    if (bus_out) {
        *bus_out = s_bus;
    }

    err = ch422g_init(&s_ch422g, s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G init failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Safe idle state:
     *   IO0/IO5 (DI) released high
     *   IO1 touch reset inactive
     *   IO2 backlight off
     *   IO3 LCD reset inactive
     *   IO4 SD CS deselected
     */
    uint8_t idle = CH422G_IO0 | CH422G_IO1 | CH422G_IO3 | CH422G_IO4 | CH422G_IO5;
    err = ch422g_write_io(s_ch422g, idle, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G idle write failed: %s", esp_err_to_name(err));
        return err;
    }

    err = board_waveshare_43b_lcd_reset();
    if (err != ESP_OK) {
        return err;
    }
    err = board_waveshare_43b_touch_reset();
    if (err != ESP_OK) {
        return err;
    }

    err = rtc_pcf85063_init(&s_rtc, s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC init failed: %s", esp_err_to_name(err));
    }

    if (board_waveshare_43b_desc.rtc_int >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << board_waveshare_43b_desc.rtc_int,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }
    if (board_waveshare_43b_desc.touch_int >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << board_waveshare_43b_desc.touch_int,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }

    s_brightness = 0;
    ESP_LOGI(TAG, "init done, rtc=%s", board_waveshare_43b_rtc_present() ? "yes" : "no");
    return ESP_OK;
}

esp_err_t board_waveshare_43b_backlight_set(bool on)
{
    if (!s_ch422g) {
        return ESP_ERR_INVALID_STATE;
    }
    return set_io_bit(CH422G_IO2, on);
}

esp_err_t board_waveshare_43b_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    return board_waveshare_43b_backlight_set(percent > 0);
}

esp_err_t board_waveshare_43b_lcd_reset(void)
{
    if (!s_ch422g) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = set_io_bit(CH422G_IO3, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    err = set_io_bit(CH422G_IO3, true);
    vTaskDelay(pdMS_TO_TICKS(120));
    return err;
}

esp_err_t board_waveshare_43b_touch_reset(void)
{
    if (!s_ch422g) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = set_io_bit(CH422G_IO1, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = set_io_bit(CH422G_IO1, true);
    vTaskDelay(pdMS_TO_TICKS(200));
    return err;
}

bool board_waveshare_43b_rtc_present(void)
{
    return rtc_pcf85063_present(s_rtc);
}

esp_err_t board_waveshare_43b_rtc_read(struct tm *out)
{
    return rtc_pcf85063_read(s_rtc, out);
}

esp_err_t board_waveshare_43b_rtc_write(const struct tm *in)
{
    return rtc_pcf85063_write(s_rtc, in);
}

esp_err_t board_waveshare_43b_io_read(uint8_t *di_bits)
{
    if (!s_ch422g || !di_bits) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw = 0;
    esp_err_t err = ch422g_read_io(s_ch422g, &raw);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t bits = 0;
    if (raw & CH422G_IO0) {
        bits |= 0x01;
    }
    if (raw & CH422G_IO5) {
        bits |= 0x02;
    }
    *di_bits = bits;
    return ESP_OK;
}

esp_err_t board_waveshare_43b_io_write(uint8_t do_bits, uint8_t mask)
{
    if (!s_ch422g) {
        return ESP_ERR_INVALID_STATE;
    }
    mask &= 0x0F;
    /* OC outputs are open-drain and active low: 1 = sink. */
    uint8_t oc_bits = (uint8_t)(~do_bits);
    return ch422g_write_oc(s_ch422g, oc_bits, mask);
}

esp_err_t board_waveshare_43b_io_get(uint8_t *di_bits, uint8_t *do_bits)
{
    if (!s_ch422g) {
        return ESP_ERR_INVALID_STATE;
    }
    if (di_bits) {
        esp_err_t err = board_waveshare_43b_io_read(di_bits);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (do_bits) {
        *do_bits = (uint8_t)(~ch422g_oc_shadow(s_ch422g) & 0x0F);
    }
    return ESP_OK;
}
