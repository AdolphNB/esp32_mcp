#include "lunar.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* Lunar year info for 1900..2099.
 * bits 0..3   : leap month (0 = none)
 * bits 4..15  : length of months 1..12 (1 = 30 days, 0 = 29 days)
 * bit 16      : length of the leap month (1 = 30 days, 0 = 29 days)
 */
static const uint32_t s_lunar_info[] = {
    0x04BD8, 0x04AE0, 0x0A570, 0x054D5, 0x0D260, 0x0D950, 0x16554, 0x056A0,
    0x09AD0, 0x055D2, 0x04AE0, 0x0A5B6, 0x0A4D0, 0x0D250, 0x1D255, 0x0B540,
    0x0D6A0, 0x0ADA2, 0x095B0, 0x14977, 0x04970, 0x0A4B0, 0x0B4B5, 0x06A50,
    0x06D40, 0x1AB54, 0x02B60, 0x09570, 0x052F2, 0x04970, 0x06566, 0x0D4A0,
    0x0EA50, 0x06E95, 0x05AD0, 0x02B60, 0x186E3, 0x092E0, 0x1C8D7, 0x0C950,
    0x0D4A0, 0x1D8A6, 0x0B550, 0x056A0, 0x1A5B4, 0x025D0, 0x092D0, 0x0D2B2,
    0x0A950, 0x0B557, 0x06CA0, 0x0B550, 0x15355, 0x04DA0, 0x0A5D0, 0x14573,
    0x052B0, 0x0A9A8, 0x0E950, 0x06AA0, 0x0AEA6, 0x0AB50, 0x04B60, 0x0AAE4,
    0x0A570, 0x05260, 0x0F263, 0x0D950, 0x05B57, 0x056A0, 0x096D0, 0x04DD5,
    0x04AD0, 0x0A4D0, 0x0D4D4, 0x0D250, 0x0D558, 0x0B540, 0x0B5A0, 0x195A6,
    0x095B0, 0x049B0, 0x0A974, 0x0A4B0, 0x0B27A, 0x06A50, 0x06D40, 0x0AF46,
    0x0AB60, 0x09570, 0x04AF5, 0x04970, 0x064B0, 0x074A3, 0x0EA50, 0x06B58,
    0x05AC0, 0x0AB60, 0x096D5, 0x092E0, 0x0C960, 0x0D954, 0x0D4A0, 0x0DA50,
    0x07552, 0x056A0, 0x0ABB7, 0x025D0, 0x092D0, 0x0CAB5, 0x0A950, 0x0B4A0,
    0x0BAA4, 0x0AD50, 0x055D9, 0x04BA0, 0x0A5B0, 0x15176, 0x052B0, 0x0A930,
    0x07954, 0x06AA0, 0x0AD50, 0x05B52, 0x04B60, 0x0A6E6, 0x0A4E0, 0x0D260,
    0x0EA65, 0x0D530, 0x05AA0, 0x076A3, 0x096D0, 0x04AFB, 0x04AD0, 0x0A4D0,
    0x1D0B6, 0x0D250, 0x0D520, 0x0DD45, 0x0B5A0, 0x056D0, 0x055B2, 0x049B0,
    0x0A577, 0x0A4B0, 0x0AA50, 0x1B255, 0x06D20, 0x0ADA0, 0x14B63, 0x09370,
    0x049F8, 0x04970, 0x064B0, 0x168A6, 0x0EA50, 0x06AA0, 0x1A6C4, 0x0AAE0,
    0x092E0, 0x0D2E3, 0x0C960, 0x0D557, 0x0D4A0, 0x0DA50, 0x05D55, 0x056A0,
    0x0A6D0, 0x055D4, 0x052D0, 0x0A9B8, 0x0A950, 0x0B4A0, 0x0B6A6, 0x0AD50,
    0x055A0, 0x0ABA4, 0x0A5B0, 0x052B0, 0x0B273, 0x06930, 0x07337, 0x06AA0,
    0x0AD50, 0x14B55, 0x04B60, 0x0A570, 0x054E4, 0x0D160, 0x0E968, 0x0D520,
    0x0DAA0, 0x16AA6, 0x056D0, 0x04AE0, 0x0A9D4, 0x0A2D0, 0x0D150, 0x0F252,
};

#define LUNAR_FIRST_YEAR 1900
#define LUNAR_LAST_YEAR  2099

static const char *s_stems[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
static const char *s_branches[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
static const char *s_zodiac[] = {"鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"};
static const char *s_month_names[] = {"", "正月", "二月", "三月", "四月", "五月", "六月",
                                      "七月", "八月", "九月", "十月", "冬月", "腊月"};
static const char *s_day_tens[] = {"初", "十", "廿", "三"};
static const char *s_digits[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};

static uint32_t year_info(int year)
{
    if (year < LUNAR_FIRST_YEAR || year > LUNAR_LAST_YEAR) {
        return 0;
    }
    return s_lunar_info[year - LUNAR_FIRST_YEAR];
}

static int leap_month(int year)
{
    return (int)(year_info(year) & 0x0F);
}

static int month_days(int year, int month, bool leap)
{
    uint32_t info = year_info(year);
    if (info == 0 || month < 1 || month > 12) {
        return 0;
    }
    if (leap) {
        return (info & 0x10000) ? 30 : 29;
    }
    return (info & (0x10000U >> month)) ? 30 : 29;
}

static int year_days(int year)
{
    uint32_t info = year_info(year);
    if (info == 0) {
        return 0;
    }
    int sum = 0;
    for (int m = 1; m <= 12; m++) {
        sum += month_days(year, m, false);
    }
    int leap = leap_month(year);
    if (leap) {
        sum += month_days(year, leap, true);
    }
    return sum;
}

/* Days since 1970-01-01 for a Gregorian date (Howard Hinnant's algorithm). */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void make_day_name(int day, char *out, size_t out_len)
{
    if (day < 1 || day > 30) {
        if (out && out_len) {
            out[0] = '\0';
        }
        return;
    }
    if (day == 10) {
        snprintf(out, out_len, "初十");
        return;
    }
    if (day == 20) {
        snprintf(out, out_len, "二十");
        return;
    }
    if (day == 30) {
        snprintf(out, out_len, "三十");
        return;
    }
    int tens = day / 10;
    int ones = day % 10;
    snprintf(out, out_len, "%s%s", s_day_tens[tens], s_digits[ones]);
}

static const char *lunar_festival(int year, int month, int day, bool leap)
{
    if (leap) {
        return "";
    }
    if (month == 1 && day == 1) return "春节";
    if (month == 1 && day == 15) return "元宵节";
    if (month == 2 && day == 2) return "龙抬头";
    if (month == 5 && day == 5) return "端午节";
    if (month == 7 && day == 7) return "七夕";
    if (month == 7 && day == 15) return "中元节";
    if (month == 8 && day == 15) return "中秋节";
    if (month == 9 && day == 9) return "重阳节";
    if (month == 12 && day == 8) return "腊八节";
    if (month == 12 && day == 23) return "小年";
    if (month == 12 && day == month_days(year, 12, false)) return "除夕";
    return "";
}

const char *lunar_solar_festival(int month, int day)
{
    if (month == 1 && day == 1) return "元旦";
    if (month == 2 && day == 14) return "情人节";
    if (month == 3 && day == 8) return "妇女节";
    if (month == 4 && day == 1) return "愚人节";
    if (month == 5 && day == 1) return "劳动节";
    if (month == 6 && day == 1) return "儿童节";
    if (month == 9 && day == 10) return "教师节";
    if (month == 10 && day == 1) return "国庆节";
    if (month == 12 && day == 24) return "平安夜";
    if (month == 12 && day == 25) return "圣诞节";
    return "";
}

esp_err_t lunar_from_time(time_t t, lunar_date_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    struct tm tm_local;
    if (!localtime_r(&t, &tm_local)) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t offset = days_from_civil(tm_local.tm_year + 1900, (unsigned)tm_local.tm_mon + 1,
                                     (unsigned)tm_local.tm_mday) -
                     days_from_civil(1900, 1, 31);
    if (offset < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int year = LUNAR_FIRST_YEAR;
    while (year <= LUNAR_LAST_YEAR) {
        int days = year_days(year);
        if (days <= 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (offset < days) {
            break;
        }
        offset -= days;
        year++;
    }
    if (year > LUNAR_LAST_YEAR) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int month = 1;
    bool leap = false;
    int day = 1;
    int leap_m = leap_month(year);
    while (month <= 12) {
        int days = month_days(year, month, false);
        if (offset < days) {
            day = (int)offset + 1;
            leap = false;
            break;
        }
        offset -= days;
        if (leap_m == month) {
            days = month_days(year, month, true);
            if (offset < days) {
                day = (int)offset + 1;
                leap = true;
                break;
            }
            offset -= days;
        }
        month++;
    }
    if (month > 12) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    out->valid = true;
    out->year = year;
    out->month = month;
    out->day = day;
    out->leap = leap;

    int gz = (year - 4) % 60;
    if (gz < 0) {
        gz += 60;
    }
    snprintf(out->year_ganzhi, sizeof(out->year_ganzhi), "%s%s",
             s_stems[gz % 10], s_branches[gz % 12]);
    snprintf(out->zodiac, sizeof(out->zodiac), "%s", s_zodiac[gz % 12]);
    snprintf(out->month_name, sizeof(out->month_name), "%s%s",
             leap ? "闰" : "", s_month_names[month]);
    make_day_name(day, out->day_name, sizeof(out->day_name));
    snprintf(out->text, sizeof(out->text), "%s%s", out->month_name, out->day_name);
    snprintf(out->festival, sizeof(out->festival), "%s",
             lunar_festival(year, month, day, leap));
    return ESP_OK;
}
