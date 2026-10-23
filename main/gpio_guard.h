#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

typedef enum {
    GPIO_GUARD_MODE_UNUSED = 0,
    GPIO_GUARD_MODE_INPUT,
    GPIO_GUARD_MODE_OUTPUT,
} gpio_guard_mode_t;

esp_err_t gpio_guard_init(void);
bool gpio_guard_is_allowed(int pin);
const char *gpio_guard_mode_str(int pin);
int gpio_guard_read(int pin, gpio_guard_mode_t *mode_out);
esp_err_t gpio_guard_write(int pin, int level, int hold_ms, int *old_level_out);
esp_err_t gpio_guard_pulse(int pin, int active_level, int duration_ms, int *old_level_out);
void gpio_guard_list_json(cJSON *array);
