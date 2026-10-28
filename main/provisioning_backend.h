#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t provisioning_backend_init(void);
void provisioning_backend_start_task(void);

#ifdef __cplusplus
}
#endif
