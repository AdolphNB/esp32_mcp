/*
 * Boot splash logo for the 4.3" display.
 *
 * The raw pixels live in assets/logo_boot.bin (460x150, little-endian RGB565,
 * background baked in) and are embedded into the firmware by CMakeLists.txt.
 * Regenerate with: ./tools/gen_boot_logo.py
 */

#include "lvgl.h"

#define BOOT_LOGO_W 460
#define BOOT_LOGO_H 150

extern const uint8_t logo_boot_bin_start[] asm("_binary_logo_boot_bin_start");
extern const uint8_t logo_boot_bin_end[] asm("_binary_logo_boot_bin_end");

const lv_image_dsc_t ui_boot_logo = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = BOOT_LOGO_W,
        .h = BOOT_LOGO_H,
        .stride = BOOT_LOGO_W * 2,
        .reserved_2 = 0,
    },
    .data_size = BOOT_LOGO_W * BOOT_LOGO_H * 2,
    .data = logo_boot_bin_start,
    .reserved = NULL,
    .reserved_2 = NULL,
};
