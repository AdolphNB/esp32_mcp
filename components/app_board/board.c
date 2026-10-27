#include "board.h"

#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board";

static const board_desc_t *s_desc;
static i2c_master_bus_handle_t s_bus;

#if CONFIG_APP_BOARD_WAVESHARE_43B
extern const board_desc_t board_waveshare_43b_desc;
esp_err_t board_waveshare_43b_init(i2c_master_bus_handle_t *bus_out);
esp_err_t board_waveshare_43b_backlight_set(bool on);
esp_err_t board_waveshare_43b_set_brightness(uint8_t percent);
esp_err_t board_waveshare_43b_touch_reset(void);
esp_err_t board_waveshare_43b_lcd_reset(void);
bool board_waveshare_43b_rtc_present(void);
esp_err_t board_waveshare_43b_rtc_read(struct tm *out);
esp_err_t board_waveshare_43b_rtc_write(const struct tm *in);
esp_err_t board_waveshare_43b_io_read(uint8_t *di_bits);
esp_err_t board_waveshare_43b_io_write(uint8_t do_bits, uint8_t mask);
esp_err_t board_waveshare_43b_io_get(uint8_t *di_bits, uint8_t *do_bits);
#elif CONFIG_APP_BOARD_YD_S3
extern const board_desc_t board_yd_s3_desc;
esp_err_t board_yd_s3_init(i2c_master_bus_handle_t *bus_out);
#endif

esp_err_t board_init(void)
{
    if (s_desc) {
        return ESP_OK;
    }

#if CONFIG_APP_BOARD_WAVESHARE_43B
    s_desc = &board_waveshare_43b_desc;
    esp_err_t err = board_waveshare_43b_init(&s_bus);
#elif CONFIG_APP_BOARD_YD_S3
    s_desc = &board_yd_s3_desc;
    esp_err_t err = board_yd_s3_init(&s_bus);
#else
#error "No board profile selected"
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
        s_desc = NULL;
        return err;
    }
    ESP_LOGI(TAG, "board=%s (%dx%d)", s_desc->id, s_desc->width, s_desc->height);
    return ESP_OK;
}

const board_desc_t *board_get(void)
{
    return s_desc;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

esp_err_t board_backlight_set(bool on)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_backlight_set(on);
#else
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_set_brightness(uint8_t percent)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_set_brightness(percent);
#else
    (void)percent;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_touch_reset(void)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_touch_reset();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_lcd_reset(void)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_lcd_reset();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool board_rtc_present(void)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_rtc_present();
#else
    return false;
#endif
}

esp_err_t board_rtc_read(struct tm *out)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_rtc_read(out);
#else
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_rtc_write(const struct tm *in)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_rtc_write(in);
#else
    (void)in;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_io_read(uint8_t *di_bits)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_io_read(di_bits);
#else
    (void)di_bits;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_io_write(uint8_t do_bits, uint8_t mask)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_io_write(do_bits, mask);
#else
    (void)do_bits;
    (void)mask;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t board_io_get(uint8_t *di_bits, uint8_t *do_bits)
{
#if CONFIG_APP_BOARD_WAVESHARE_43B
    return board_waveshare_43b_io_get(di_bits, do_bits);
#else
    (void)di_bits;
    (void)do_bits;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void board_info_json(cJSON *root)
{
    if (!root || !s_desc) {
        return;
    }
    cJSON_AddStringToObject(root, "id", s_desc->id);
    cJSON_AddStringToObject(root, "name", s_desc->name);
    cJSON_AddNumberToObject(root, "width", s_desc->width);
    cJSON_AddNumberToObject(root, "height", s_desc->height);
    cJSON_AddBoolToObject(root, "display", s_desc->has_display);
    cJSON_AddBoolToObject(root, "touch", s_desc->has_touch);
    cJSON_AddBoolToObject(root, "rtc", s_desc->has_rtc);
    cJSON_AddBoolToObject(root, "isolated_io", s_desc->has_isolated_io);
    cJSON_AddBoolToObject(root, "rs485", s_desc->has_rs485);
    cJSON_AddBoolToObject(root, "can", s_desc->has_can);
    cJSON_AddBoolToObject(root, "gpio_header", s_desc->has_gpio_header);
    cJSON_AddNumberToObject(root, "i2c_sda", s_desc->i2c_sda);
    cJSON_AddNumberToObject(root, "i2c_scl", s_desc->i2c_scl);
    cJSON_AddNumberToObject(root, "rs485_tx", s_desc->rs485_tx);
    cJSON_AddNumberToObject(root, "rs485_rx", s_desc->rs485_rx);
    cJSON_AddNumberToObject(root, "can_tx", s_desc->can_tx);
    cJSON_AddNumberToObject(root, "can_rx", s_desc->can_rx);
    cJSON_AddNumberToObject(root, "led_gpio", s_desc->led_gpio);
}
