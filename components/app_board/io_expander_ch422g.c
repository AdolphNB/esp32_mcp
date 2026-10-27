#include "io_expander_ch422g.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ch422g";

#define CH422G_I2C_TIMEOUT_MS 100

struct ch422g_s {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev_mode;
    i2c_master_dev_handle_t dev_oc;
    i2c_master_dev_handle_t dev_io;
    i2c_master_dev_handle_t dev_in;
    SemaphoreHandle_t lock;
    uint8_t io_shadow;
    uint8_t oc_shadow;
    uint8_t mode;
    bool ready;
};

static esp_err_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

static esp_err_t write_byte(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, CH422G_I2C_TIMEOUT_MS);
}

esp_err_t ch422g_init(ch422g_t **out, i2c_master_bus_handle_t bus)
{
    if (!out || !bus) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    ch422g_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    dev->bus = bus;
    dev->lock = xSemaphoreCreateMutex();
    if (!dev->lock) {
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = add_dev(bus, CH422G_ADDR_MODE, &dev->dev_mode);
    if (err == ESP_OK) {
        err = add_dev(bus, CH422G_ADDR_OC, &dev->dev_oc);
    }
    if (err == ESP_OK) {
        err = add_dev(bus, CH422G_ADDR_IO, &dev->dev_io);
    }
    if (err == ESP_OK) {
        err = add_dev(bus, CH422G_ADDR_IN, &dev->dev_in);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add I2C device: %s", esp_err_to_name(err));
        ch422g_deinit(dev);
        return err;
    }

    /* Enable both the quasi-bidirectional IO outputs and the open-drain outputs. */
    dev->mode = CH422G_MODE_IO_OUTPUT | CH422G_MODE_OD_ENABLE;
    err = write_byte(dev->dev_mode, dev->mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mode write failed: %s", esp_err_to_name(err));
        ch422g_deinit(dev);
        return err;
    }

    /* Safe idle: backlight off, resets inactive, SD deselected, DOs released. */
    dev->io_shadow = CH422G_IO0 | CH422G_IO1 | CH422G_IO3 | CH422G_IO4 | CH422G_IO5;
    dev->oc_shadow = 0xFF;
    err = write_byte(dev->dev_io, dev->io_shadow);
    if (err == ESP_OK) {
        err = write_byte(dev->dev_oc, dev->oc_shadow);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "idle write failed: %s", esp_err_to_name(err));
        ch422g_deinit(dev);
        return err;
    }

    dev->ready = true;
    *out = dev;
    return ESP_OK;
}

void ch422g_deinit(ch422g_t *dev)
{
    if (!dev) {
        return;
    }
    if (dev->dev_mode) {
        i2c_master_bus_rm_device(dev->dev_mode);
    }
    if (dev->dev_oc) {
        i2c_master_bus_rm_device(dev->dev_oc);
    }
    if (dev->dev_io) {
        i2c_master_bus_rm_device(dev->dev_io);
    }
    if (dev->dev_in) {
        i2c_master_bus_rm_device(dev->dev_in);
    }
    if (dev->lock) {
        vSemaphoreDelete(dev->lock);
    }
    free(dev);
}

esp_err_t ch422g_set_mode(ch422g_t *dev, uint8_t mode)
{
    if (!dev || !dev->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = write_byte(dev->dev_mode, mode);
    if (err == ESP_OK) {
        dev->mode = mode;
    }
    xSemaphoreGive(dev->lock);
    return err;
}

esp_err_t ch422g_write_io(ch422g_t *dev, uint8_t bits, uint8_t mask)
{
    if (!dev || !dev->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t next = (uint8_t)((dev->io_shadow & ~mask) | (bits & mask));
    esp_err_t err = write_byte(dev->dev_io, next);
    if (err == ESP_OK) {
        dev->io_shadow = next;
    }
    xSemaphoreGive(dev->lock);
    return err;
}

esp_err_t ch422g_write_oc(ch422g_t *dev, uint8_t bits, uint8_t mask)
{
    if (!dev || !dev->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t next = (uint8_t)((dev->oc_shadow & ~mask) | (bits & mask));
    esp_err_t err = write_byte(dev->dev_oc, next);
    if (err == ESP_OK) {
        dev->oc_shadow = next;
    }
    xSemaphoreGive(dev->lock);
    return err;
}

esp_err_t ch422g_read_io(ch422g_t *dev, uint8_t *bits)
{
    if (!dev || !dev->ready || !bits) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(dev->lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t value = 0;
    /*
     * Try reading the input register without touching the global mode first.
     * If the chip requires input mode for a valid read, fall back to a short
     * mode switch and restore the output mode + shadow registers.
     */
    esp_err_t err = i2c_master_receive(dev->dev_in, &value, 1, CH422G_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *bits = value;
        xSemaphoreGive(dev->lock);
        return ESP_OK;
    }

    err = write_byte(dev->dev_mode, 0x00);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev->dev_in, &value, 1, CH422G_I2C_TIMEOUT_MS);
    }
    esp_err_t restore_err = write_byte(dev->dev_mode, dev->mode);
    if (restore_err == ESP_OK) {
        restore_err = write_byte(dev->dev_io, dev->io_shadow);
    }
    if (restore_err == ESP_OK) {
        restore_err = write_byte(dev->dev_oc, dev->oc_shadow);
    }

    xSemaphoreGive(dev->lock);
    if (err == ESP_OK) {
        *bits = value;
    }
    return err != ESP_OK ? err : restore_err;
}

uint8_t ch422g_io_shadow(const ch422g_t *dev)
{
    return dev ? dev->io_shadow : 0;
}

uint8_t ch422g_oc_shadow(const ch422g_t *dev)
{
    return dev ? dev->oc_shadow : 0;
}
