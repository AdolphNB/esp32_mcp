#include "clock_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "app_config.h"
#include "board.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "lunar.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

static const char *TAG = "clock";

static bool s_sntp_started;
static bool s_synced;

static const char *weekday_cn(int wday)
{
    static const char *names[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    if (wday < 0 || wday > 6) {
        return "";
    }
    return names[wday];
}

static esp_err_t set_system_time(const struct tm *local)
{
    if (!local) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm copy = *local;
    copy.tm_isdst = -1;
    time_t t = mktime(&copy);
    if (t < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    time_t now = tv ? tv->tv_sec : time(NULL);
    struct tm local;
    if (localtime_r(&now, &local)) {
        if (board_rtc_present()) {
            esp_err_t err = board_rtc_write(&local);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "RTC write after SNTP failed: %s", esp_err_to_name(err));
            }
        }
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
        ESP_LOGI(TAG, "SNTP synced: %s", buf);
    }
}

esp_err_t clock_service_init(void)
{
    setenv("TZ", CONFIG_APP_DISPLAY_TIMEZONE, 1);
    tzset();

    struct tm rtc = {0};
    if (board_rtc_present() && board_rtc_read(&rtc) == ESP_OK) {
        if (set_system_time(&rtc) == ESP_OK) {
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &rtc);
            ESP_LOGI(TAG, "system time restored from RTC: %s", buf);
        }
    } else {
        ESP_LOGW(TAG, "no valid RTC time, waiting for SNTP");
    }
    return ESP_OK;
}

esp_err_t clock_service_start_sntp(void)
{
    if (s_sntp_started) {
        return ESP_OK;
    }
    s_sntp_started = true;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_APP_DISPLAY_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started, server=%s tz=%s", CONFIG_APP_DISPLAY_NTP_SERVER,
             CONFIG_APP_DISPLAY_TIMEZONE);
    return ESP_OK;
}

bool clock_service_time_valid(void)
{
    time_t now = time(NULL);
    return now > 1700000000; /* 2023-11-14 */
}

esp_err_t clock_service_get_time(struct tm *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now = time(NULL);
    if (!localtime_r(&now, out)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t clock_service_set_time(const struct tm *in)
{
    esp_err_t err = set_system_time(in);
    if (err != ESP_OK) {
        return err;
    }
    if (board_rtc_present()) {
        struct tm local = *in;
        local.tm_isdst = -1;
        err = board_rtc_write(&local);
    }
    return err;
}

static void add_lunar_json(cJSON *root, time_t now, const struct tm *local)
{
    lunar_date_t lunar;
    if (lunar_from_time(now, &lunar) != ESP_OK || !lunar.valid) {
        return;
    }
    cJSON *lj = cJSON_AddObjectToObject(root, "lunar");
    if (!lj) {
        return;
    }
    cJSON_AddBoolToObject(lj, "valid", true);
    cJSON_AddNumberToObject(lj, "year", lunar.year);
    cJSON_AddNumberToObject(lj, "month", lunar.month);
    cJSON_AddNumberToObject(lj, "day", lunar.day);
    cJSON_AddBoolToObject(lj, "leap", lunar.leap);
    cJSON_AddStringToObject(lj, "text", lunar.text);
    cJSON_AddStringToObject(lj, "month_name", lunar.month_name);
    cJSON_AddStringToObject(lj, "day_name", lunar.day_name);
    cJSON_AddStringToObject(lj, "ganzhi", lunar.year_ganzhi);
    cJSON_AddStringToObject(lj, "zodiac", lunar.zodiac);

    const char *festival = lunar.festival;
    if (!festival[0]) {
        festival = lunar_solar_festival(local->tm_mon + 1, local->tm_mday);
    }
    cJSON_AddStringToObject(lj, "festival", festival);
}

void clock_service_status_json(cJSON *root)
{
    if (!root) {
        return;
    }
    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);

    char iso[40] = {0};
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S%z", &local);
    char date[16] = {0};
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    char time_s[16] = {0};
    strftime(time_s, sizeof(time_s), "%H:%M:%S", &local);

    cJSON_AddNumberToObject(root, "unix", (double)now);
    cJSON_AddStringToObject(root, "iso", iso);
    cJSON_AddStringToObject(root, "date", date);
    cJSON_AddStringToObject(root, "time", time_s);
    cJSON_AddStringToObject(root, "weekday", weekday_cn(local.tm_wday));
    cJSON_AddStringToObject(root, "timezone", CONFIG_APP_DISPLAY_TIMEZONE);
    cJSON_AddBoolToObject(root, "time_valid", clock_service_time_valid());
    cJSON_AddBoolToObject(root, "sntp_synced", s_synced ||
                          (s_sntp_started && esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED));
    cJSON_AddBoolToObject(root, "rtc_present", board_rtc_present());

    add_lunar_json(root, now, &local);
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */
