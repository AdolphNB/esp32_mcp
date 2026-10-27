#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_DI_COUNT 2
#define BOARD_DO_COUNT 4

typedef struct {
    const char *id;
    const char *name;
    int width;
    int height;

    bool has_display;
    bool has_touch;
    bool has_rtc;
    bool has_io_expander;
    bool has_isolated_io;
    bool has_rs485;
    bool has_can;
    bool has_backlight_pwm;
    bool has_gpio_header;

    int i2c_sda;
    int i2c_scl;
    int lcd_vsync;
    int lcd_hsync;
    int lcd_de;
    int lcd_pclk;
    int lcd_data[16];
    int touch_int;
    int rtc_int;
    int rs485_tx;
    int rs485_rx;
    int can_tx;
    int can_rx;
    int led_gpio;
} board_desc_t;

esp_err_t board_init(void);
const board_desc_t *board_get(void);

i2c_master_bus_handle_t board_i2c_bus(void);

esp_err_t board_backlight_set(bool on);
esp_err_t board_set_brightness(uint8_t percent);

esp_err_t board_touch_reset(void);
esp_err_t board_lcd_reset(void);

bool board_rtc_present(void);
esp_err_t board_rtc_read(struct tm *out);
esp_err_t board_rtc_write(const struct tm *in);

esp_err_t board_io_read(uint8_t *di_bits);
esp_err_t board_io_write(uint8_t do_bits, uint8_t mask);
esp_err_t board_io_get(uint8_t *di_bits, uint8_t *do_bits);

void board_info_json(cJSON *root);

#ifdef __cplusplus
}
#endif
