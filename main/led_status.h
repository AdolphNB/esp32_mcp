#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t led_status_init(void);
esp_err_t led_status_set(bool on, const char *color, int brightness);
