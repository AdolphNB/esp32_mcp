#include "rtc_pcf85063.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "pcf85063";

#define I2C_TIMEOUT_MS 100

#define REG_CTRL1  0x00
#define REG_SECOND 0x04
#define REG_MINUTE 0x05
#define REG_HOUR   0x06
#define REG_DAY    0x07
#define REG_WDAY   0x08
#define REG_MONTH  0x09
#define REG_YEAR   0x0A

struct rtc_pcf85063_s {
    i2c_master_dev_handle_t dev;
    SemaphoreHandle_t lock;
    bool present;
};

static uint8_t bcd_to_dec(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

static uint8_t dec_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int build_year(void)
{
    /* __DATE__ is "Mmm dd yyyy". */
    static int year;
    if (year == 0) {
        const char *p = __DATE__ + 7;
        year = atoi(p);
        if (year < 2020) {
            year = 2026;
        }
    }
    return year;
}

/*
 * PCF85063 only stores a 2-digit year. Waveshare's demo uses 1970 + reg,
 * while most RTC tooling uses 2000 + reg. Pick the interpretation closest to
 * the firmware build year so both conventions decode sanely.
 */
static int decode_year(uint8_t reg)
{
    int y2000 = 2000 + reg;
    int y1970 = 1970 + reg;
    int ref = build_year();
    int d2000 = y2000 > ref ? y2000 - ref : ref - y2000;
    int d1970 = y1970 > ref ? y1970 - ref : ref - y1970;
    return d2000 <= d1970 ? y2000 : y1970;
}

esp_err_t rtc_pcf85063_init(rtc_pcf85063_t **out, i2c_master_bus_handle_t bus)
{
    if (!out || !bus) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    rtc_pcf85063_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    dev->lock = xSemaphoreCreateMutex();
    if (!dev->lock) {
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev->dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to add device: %s", esp_err_to_name(err));
        rtc_pcf85063_deinit(dev);
        return err;
    }

    /* Probe: a one-byte read of the seconds register. */
    uint8_t reg = REG_SECOND;
    uint8_t sec = 0;
    err = i2c_master_transmit_receive(dev->dev, &reg, 1, &sec, 1, I2C_TIMEOUT_MS);
    dev->present = (err == ESP_OK);
    if (dev->present) {
        /* 24-hour mode, running (STOP = 0). */
        uint8_t ctrl[2] = {REG_CTRL1, 0x00};
        i2c_master_transmit(dev->dev, ctrl, sizeof(ctrl), I2C_TIMEOUT_MS);
        ESP_LOGI(TAG, "PCF85063 detected at 0x%02X", PCF85063_I2C_ADDR);
    } else {
        ESP_LOGW(TAG, "PCF85063 not detected: %s", esp_err_to_name(err));
    }

    *out = dev;
    return ESP_OK;
}

void rtc_pcf85063_deinit(rtc_pcf85063_t *dev)
{
    if (!dev) {
        return;
    }
    if (dev->dev) {
        i2c_master_bus_rm_device(dev->dev);
    }
    if (dev->lock) {
        vSemaphoreDelete(dev->lock);
    }
    free(dev);
}

bool rtc_pcf85063_present(const rtc_pcf85063_t *dev)
{
    return dev && dev->present;
}

esp_err_t rtc_pcf85063_read(rtc_pcf85063_t *dev, struct tm *out)
{
    if (!dev || !dev->present || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t reg = REG_SECOND;
    uint8_t buf[7] = {0};
    esp_err_t err = i2c_master_transmit_receive(dev->dev, &reg, 1, buf, sizeof(buf), I2C_TIMEOUT_MS);
    xSemaphoreGive(dev->lock);
    if (err != ESP_OK) {
        return err;
    }

    struct tm t = {0};
    t.tm_sec = bcd_to_dec(buf[0] & 0x7F);
    t.tm_min = bcd_to_dec(buf[1] & 0x7F);
    t.tm_hour = bcd_to_dec(buf[2] & 0x3F);
    t.tm_mday = bcd_to_dec(buf[3] & 0x3F);
    t.tm_wday = bcd_to_dec(buf[4] & 0x07);
    t.tm_mon = (int)bcd_to_dec(buf[5] & 0x1F) - 1;
    t.tm_year = decode_year(bcd_to_dec(buf[6])) - 1900;

    if (t.tm_sec > 59 || t.tm_min > 59 || t.tm_hour > 23 ||
        t.tm_mday < 1 || t.tm_mday > 31 || t.tm_mon < 0 || t.tm_mon > 11 ||
        t.tm_year < 120 || t.tm_year > 199) { /* 2020..2099 */
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out = t;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_write(rtc_pcf85063_t *dev, const struct tm *in)
{
    if (!dev || !dev->present || !in) {
        return ESP_ERR_INVALID_STATE;
    }
    if (in->tm_year < 100 || in->tm_year > 199 ||
        in->tm_mon < 0 || in->tm_mon > 11 ||
        in->tm_mday < 1 || in->tm_mday > 31 ||
        in->tm_hour < 0 || in->tm_hour > 23 ||
        in->tm_min < 0 || in->tm_min > 59 ||
        in->tm_sec < 0 || in->tm_sec > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[8] = {
        REG_SECOND,
        dec_to_bcd((uint8_t)in->tm_sec),
        dec_to_bcd((uint8_t)in->tm_min),
        dec_to_bcd((uint8_t)in->tm_hour),
        dec_to_bcd((uint8_t)in->tm_mday),
        dec_to_bcd((uint8_t)in->tm_wday),
        dec_to_bcd((uint8_t)(in->tm_mon + 1)),
        dec_to_bcd((uint8_t)(in->tm_year - 100)),
    };

    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
    xSemaphoreGive(dev->lock);
    return err;
}
