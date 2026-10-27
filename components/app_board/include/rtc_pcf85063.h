#pragma once

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR 0x51

typedef struct rtc_pcf85063_s rtc_pcf85063_t;

esp_err_t rtc_pcf85063_init(rtc_pcf85063_t **out, i2c_master_bus_handle_t bus);
void rtc_pcf85063_deinit(rtc_pcf85063_t *dev);

bool rtc_pcf85063_present(const rtc_pcf85063_t *dev);

esp_err_t rtc_pcf85063_read(rtc_pcf85063_t *dev, struct tm *out);
esp_err_t rtc_pcf85063_write(rtc_pcf85063_t *dev, const struct tm *in);

#ifdef __cplusplus
}
#endif
