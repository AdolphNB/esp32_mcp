#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int year;
    int month;
    int day;
    bool leap;

    char year_ganzhi[8];
    char zodiac[8];
    char month_name[12];
    char day_name[12];
    char text[32];
    char festival[24];
} lunar_date_t;

esp_err_t lunar_from_time(time_t t, lunar_date_t *out);
const char *lunar_solar_festival(int month, int day);

#ifdef __cplusplus
}
#endif
