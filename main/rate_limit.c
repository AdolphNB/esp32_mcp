#include "rate_limit.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static int s_rate;
static int s_burst;
static int s_tokens;
static int64_t s_last_us;

esp_err_t rate_limit_init(int calls_per_second, int burst)
{
    if (calls_per_second <= 0 || burst <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_rate = calls_per_second;
    s_burst = burst;
    s_tokens = burst;
    s_last_us = esp_timer_get_time();
    return ESP_OK;
}

bool rate_limit_allow(void)
{
    if (!s_lock) {
        return true;
    }
    bool allowed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - s_last_us;
    if (elapsed > 0) {
        int add = (int)((elapsed * s_rate) / 1000000);
        if (add > 0) {
            s_tokens += add;
            if (s_tokens > s_burst) {
                s_tokens = s_burst;
            }
            s_last_us += (int64_t)add * 1000000 / s_rate;
        }
    }
    if (s_tokens > 0) {
        s_tokens--;
        allowed = true;
    }
    xSemaphoreGive(s_lock);
    return allowed;
}
