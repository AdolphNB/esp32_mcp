#include "gpio_guard.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/gpio_types.h"

#define GPIO_MAX_PIN 48

static const char *TAG = "gpio_guard";

typedef struct {
    bool active;
    int old_level;
    gpio_guard_mode_t old_mode;
} restore_state_t;

static uint64_t s_allowed_mask;
static gpio_guard_mode_t s_modes[GPIO_MAX_PIN + 1];
static restore_state_t s_restore[GPIO_MAX_PIN + 1];
static esp_timer_handle_t s_timers[GPIO_MAX_PIN + 1];
static SemaphoreHandle_t s_lock;

static const char *mode_str(gpio_guard_mode_t mode)
{
    switch (mode) {
    case GPIO_GUARD_MODE_INPUT: return "input";
    case GPIO_GUARD_MODE_OUTPUT: return "output";
    default: return "unused";
    }
}

static void restore_cb(void *arg)
{
    int pin = (int)(intptr_t)arg;
    if (pin < 0 || pin > GPIO_MAX_PIN) {
        return;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (s_restore[pin].active) {
            gpio_set_level((gpio_num_t)pin, s_restore[pin].old_level);
            gpio_set_direction((gpio_num_t)pin,
                s_restore[pin].old_mode == GPIO_GUARD_MODE_OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
            s_modes[pin] = s_restore[pin].old_mode;
            s_restore[pin].active = false;
        }
        xSemaphoreGive(s_lock);
    }
}

static void stop_restore_locked(int pin)
{
    if (pin < 0 || pin > GPIO_MAX_PIN) {
        return;
    }
    if (s_timers[pin]) {
        esp_timer_stop(s_timers[pin]);
    }
    s_restore[pin].active = false;
}

esp_err_t gpio_guard_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_allowed_mask = 0;

    char list[128];
    snprintf(list, sizeof(list), "%s", APP_GPIO_ALLOWLIST);

    char *save = NULL;
    char *tok = strtok_r(list, ",", &save);
    while (tok) {
        while (*tok == ' ') {
            tok++;
        }
        char *end = NULL;
        long pin = strtol(tok, &end, 10);
        if (end != tok && pin >= 0 && pin <= GPIO_MAX_PIN) {
            s_allowed_mask |= (1ULL << pin);
            s_modes[pin] = GPIO_GUARD_MODE_INPUT;

            gpio_config_t cfg = {
                .pin_bit_mask = (1ULL << pin),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            esp_err_t err = gpio_config(&cfg);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "gpio_config(%ld) failed: %s", pin, esp_err_to_name(err));
            }

            esp_timer_create_args_t targs = {
                .callback = restore_cb,
                .arg = (void *)(intptr_t)pin,
                .name = "gpio_restore",
            };
            if (esp_timer_create(&targs, &s_timers[pin]) != ESP_OK) {
                ESP_LOGW(TAG, "timer create failed for GPIO%ld", pin);
                s_timers[pin] = NULL;
            }
        } else {
            ESP_LOGW(TAG, "ignoring invalid GPIO entry '%s'", tok);
        }
        tok = strtok_r(NULL, ",", &save);
    }

    char pins[96];
    size_t off = 0;
    pins[0] = '\0';
    for (int i = 0; i <= GPIO_MAX_PIN && off + 4 < sizeof(pins); i++) {
        if (s_allowed_mask & (1ULL << i)) {
            int n = snprintf(pins + off, sizeof(pins) - off, "%s%d", off ? "," : "", i);
            if (n > 0) {
                off += (size_t)n;
            }
        }
    }
    /* Log the pin list rather than the raw 64-bit mask: a value like
     * 0x40002781f0 contains an 8-digit `0x4xxxxxxx` substring that the
     * idf.py monitor panic decoder mistakes for a ROM address. */
    ESP_LOGI(TAG, "allowed GPIO pins: %s", pins);
    return ESP_OK;
}

bool gpio_guard_is_allowed(int pin)
{
    return pin >= 0 && pin <= GPIO_MAX_PIN && (s_allowed_mask & (1ULL << pin)) != 0;
}

const char *gpio_guard_mode_str(int pin)
{
    if (pin < 0 || pin > GPIO_MAX_PIN) {
        return "unused";
    }
    return mode_str(s_modes[pin]);
}

int gpio_guard_read(int pin, gpio_guard_mode_t *mode_out)
{
    if (mode_out) {
        *mode_out = (pin >= 0 && pin <= GPIO_MAX_PIN) ? s_modes[pin] : GPIO_GUARD_MODE_UNUSED;
    }
    if (!gpio_guard_is_allowed(pin)) {
        return -1;
    }
    return gpio_get_level((gpio_num_t)pin);
}

esp_err_t gpio_guard_write(int pin, int level, int hold_ms, int *old_level_out)
{
    if (!gpio_guard_is_allowed(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
#if !CONFIG_APP_GPIO_ALLOW_WRITE
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (hold_ms < 0 || hold_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    stop_restore_locked(pin);

    int old_level = gpio_get_level((gpio_num_t)pin);
    gpio_guard_mode_t old_mode = s_modes[pin];
    if (old_level_out) {
        *old_level_out = old_level;
    }

    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, level);
    s_modes[pin] = GPIO_GUARD_MODE_OUTPUT;

    if (hold_ms > 0 && s_timers[pin]) {
        s_restore[pin].old_level = old_level;
        s_restore[pin].old_mode = old_mode;
        s_restore[pin].active = true;
        esp_timer_start_once(s_timers[pin], (uint64_t)hold_ms * 1000ULL);
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t gpio_guard_pulse(int pin, int active_level, int duration_ms, int *old_level_out)
{
    if (duration_ms < 10 || duration_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_guard_write(pin, active_level, duration_ms, old_level_out);
}

void gpio_guard_list_json(cJSON *array)
{
    if (!array) {
        return;
    }
    for (int pin = 0; pin <= GPIO_MAX_PIN; pin++) {
        if (!gpio_guard_is_allowed(pin)) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            return;
        }
        cJSON_AddNumberToObject(item, "pin", pin);
        cJSON_AddNumberToObject(item, "level", gpio_get_level((gpio_num_t)pin));
        cJSON_AddStringToObject(item, "mode", mode_str(s_modes[pin]));
        cJSON_AddItemToArray(array, item);
    }
}
