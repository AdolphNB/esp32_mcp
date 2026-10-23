#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t nvs_store_init(void);

const char *nvs_store_get_token(void);
const char *nvs_store_get_hostname(void);

bool nvs_store_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_store_set_wifi(const char *ssid, const char *pass, bool configured);
esp_err_t nvs_store_clear_wifi(void);
esp_err_t nvs_store_clear_all(void);

esp_err_t nvs_store_get_str(const char *key, char *out, size_t out_len);
esp_err_t nvs_store_set_str(const char *key, const char *value);
esp_err_t nvs_store_get_u8(const char *key, uint8_t *out);
esp_err_t nvs_store_set_u8(const char *key, uint8_t value);
