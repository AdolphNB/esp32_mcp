#include <stdio.h>

#include "app_config.h"
#include "captive_portal.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_guard.h"
#include "led_status.h"
#include "mcp_server.h"
#include "mdns_service.h"
#include "nvs_store.h"
#include "ota.h"
#include "rate_limit.h"
#include "wifi_manager.h"

static const char *TAG = "app";

static void network_task(void *arg)
{
    (void)arg;
    bool mcp_started = false;
    bool mdns_started = false;

    while (true) {
        bool connected = wifi_manager_is_connected();
        if (connected && !mcp_started) {
            if (mcp_server_start() == ESP_OK) {
                mcp_started = true;
                led_status_set(true, "#00FF00", 10);
            } else {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        if (connected && mcp_started && !mdns_started) {
            if (mdns_service_start() == ESP_OK) {
                mdns_started = true;
            } else {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

#if CONFIG_APP_MCP_IN_AP_MODE
        if (!mcp_started && wifi_manager_is_ap_active()) {
            if (mcp_server_start() == ESP_OK) {
                mcp_started = true;
            }
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int held = 0;
    while (true) {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            held++;
            if (held >= 50) {
                ESP_LOGW(TAG, "BOOT held for 5s, erasing configuration");
                nvs_store_clear_all();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            held = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 MCP Node v%s", esp_app_get_description()->version);

    ESP_ERROR_CHECK(nvs_store_init());
    ESP_ERROR_CHECK(ota_init());

    ESP_ERROR_CHECK(gpio_guard_init());
    led_status_init();
    led_status_set(true, "#0000FF", 10);
    vTaskDelay(pdMS_TO_TICKS(250));
    led_status_set(false, NULL, 0);

    ESP_ERROR_CHECK(rate_limit_init(CONFIG_APP_MCP_RATE_LIMIT, CONFIG_APP_MCP_RATE_LIMIT * 2));
    ESP_ERROR_CHECK(mcp_server_init());

    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(captive_portal_start());
    ESP_ERROR_CHECK(wifi_manager_start());

    xTaskCreate(network_task, "network_task", 8192, NULL, 4, NULL);
    xTaskCreate(factory_reset_task, "factory_reset", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "boot complete, state=%s", wifi_manager_state_str());
}
