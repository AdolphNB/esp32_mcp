#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validate and normalize a scene JSON document.
 *
 * On success *normalized receives a heap-allocated JSON string. The caller must
 * free it with ui_scene_free(). On failure an error message is written to err.
 */
esp_err_t ui_scene_validate(const char *json, char **normalized, char *err, size_t err_len);

void ui_scene_free(char *json);

esp_err_t ui_scene_default_home(char **out_json);

#ifdef __cplusplus
}
#endif
