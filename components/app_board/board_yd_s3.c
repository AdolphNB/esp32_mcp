#include "board.h"

#include "esp_err.h"

const board_desc_t board_yd_s3_desc = {
    .id = "yd_s3",
    .name = "YD-ESP32-S3 N16R8",
    .width = 0,
    .height = 0,
    .has_display = false,
    .has_touch = false,
    .has_rtc = false,
    .has_io_expander = false,
    .has_isolated_io = false,
    .has_rs485 = false,
    .has_can = false,
    .has_backlight_pwm = false,
    .has_gpio_header = true,
    .i2c_sda = -1,
    .i2c_scl = -1,
    .lcd_vsync = -1,
    .lcd_hsync = -1,
    .lcd_de = -1,
    .lcd_pclk = -1,
    .lcd_data = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    .touch_int = -1,
    .rtc_int = -1,
    .rs485_tx = -1,
    .rs485_rx = -1,
    .can_tx = -1,
    .can_rx = -1,
    .led_gpio = 48,
};

esp_err_t board_yd_s3_init(i2c_master_bus_handle_t *bus_out)
{
    if (bus_out) {
        *bus_out = NULL;
    }
    return ESP_OK;
}
