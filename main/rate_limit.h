#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t rate_limit_init(int calls_per_second, int burst);
bool rate_limit_allow(void);
