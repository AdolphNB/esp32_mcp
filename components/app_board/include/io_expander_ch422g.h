#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CH422G register aliases (each is a separate I2C slave address). */
#define CH422G_ADDR_MODE 0x24
#define CH422G_ADDR_OC   0x23
#define CH422G_ADDR_IO   0x38
#define CH422G_ADDR_IN   0x26

#define CH422G_MODE_IO_OUTPUT 0x01
#define CH422G_MODE_A_SCAN    0x04
#define CH422G_MODE_OD_ENABLE 0x10
#define CH422G_MODE_SLEEP     0x80

/* IO output bits */
#define CH422G_IO0 0x01 /* isolated DI0 */
#define CH422G_IO1 0x02 /* touch reset */
#define CH422G_IO2 0x04 /* backlight enable */
#define CH422G_IO3 0x08 /* LCD reset */
#define CH422G_IO4 0x10 /* SD card CS */
#define CH422G_IO5 0x20 /* isolated DI1 */
#define CH422G_IO6 0x40
#define CH422G_IO7 0x80

/* OC output bits (DO0..DO3) */
#define CH422G_OC0 0x01
#define CH422G_OC1 0x02
#define CH422G_OC2 0x04
#define CH422G_OC3 0x08

typedef struct ch422g_s ch422g_t;

esp_err_t ch422g_init(ch422g_t **out, i2c_master_bus_handle_t bus);
void ch422g_deinit(ch422g_t *dev);

esp_err_t ch422g_set_mode(ch422g_t *dev, uint8_t mode);

esp_err_t ch422g_write_io(ch422g_t *dev, uint8_t bits, uint8_t mask);
esp_err_t ch422g_write_oc(ch422g_t *dev, uint8_t bits, uint8_t mask);
esp_err_t ch422g_read_io(ch422g_t *dev, uint8_t *bits);

uint8_t ch422g_io_shadow(const ch422g_t *dev);
uint8_t ch422g_oc_shadow(const ch422g_t *dev);

#ifdef __cplusplus
}
#endif
