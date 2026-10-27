#include "mcp_tools.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "gpio_guard.h"
#include "led_status.h"
#include "mcp_tools_common.h"
#include "rate_limit.h"

static esp_err_t gpio_list_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "writable", APP_GPIO_ALLOW_WRITE);
    cJSON *pins = cJSON_AddArrayToObject(root, "pins");
    mcp_gpio_snapshot_fill(pins);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t gpio_read_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int pin = esp_mcp_property_list_get_property_int(properties, "pin");
    if (!gpio_guard_is_allowed(pin)) {
        return mcp_tool_reply_error(result, "pin_not_allowed", "GPIO is not in the allowlist");
    }

    gpio_guard_mode_t mode;
    int level = gpio_guard_read(pin, &mode);
    if (level < 0) {
        return mcp_tool_reply_error(result, "internal", "read failed");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddNumberToObject(root, "pin", pin);
    cJSON_AddNumberToObject(root, "level", level);
    cJSON_AddStringToObject(root, "mode", gpio_guard_mode_str(pin));
    cJSON_AddBoolToObject(root, "allowed", true);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t gpio_write_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int pin = esp_mcp_property_list_get_property_int(properties, "pin");
    const char *level_s = esp_mcp_property_list_get_property_string(properties, "level");
    int hold_ms = esp_mcp_property_list_get_property_int(properties, "hold_ms");

    if (!gpio_guard_is_allowed(pin)) {
        return mcp_tool_reply_error(result, "pin_not_allowed", "GPIO is not in the allowlist");
    }
    if (!level_s || (strcmp(level_s, "high") != 0 && strcmp(level_s, "low") != 0)) {
        return mcp_tool_reply_error(result, "invalid_args", "level must be high or low");
    }
    if (hold_ms < 0 || hold_ms > 60000) {
        return mcp_tool_reply_error(result, "invalid_args", "hold_ms must be 0..60000");
    }

    int level = strcmp(level_s, "high") == 0 ? 1 : 0;
    int old_level = -1;
    esp_err_t err = gpio_guard_write(pin, level, hold_ms, &old_level);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return mcp_tool_reply_error(result, "pin_not_output_capable", "GPIO cannot be driven");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "internal", esp_err_to_name(err));
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddNumberToObject(root, "pin", pin);
    cJSON_AddNumberToObject(root, "level", level);
    cJSON_AddNumberToObject(root, "old_level", old_level);
    cJSON_AddNumberToObject(root, "revert_in_ms", hold_ms);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t gpio_pulse_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    int pin = esp_mcp_property_list_get_property_int(properties, "pin");
    int duration_ms = esp_mcp_property_list_get_property_int(properties, "duration_ms");
    const char *active_s = esp_mcp_property_list_get_property_string(properties, "active_level");

    if (!gpio_guard_is_allowed(pin)) {
        return mcp_tool_reply_error(result, "pin_not_allowed", "GPIO is not in the allowlist");
    }
    if (duration_ms < 10 || duration_ms > 5000) {
        return mcp_tool_reply_error(result, "invalid_args", "duration_ms must be 10..5000");
    }
    int active = 1;
    if (active_s) {
        if (strcmp(active_s, "high") == 0) {
            active = 1;
        } else if (strcmp(active_s, "low") == 0) {
            active = 0;
        } else {
            return mcp_tool_reply_error(result, "invalid_args", "active_level must be high or low");
        }
    }

    int old_level = -1;
    esp_err_t err = gpio_guard_pulse(pin, active, duration_ms, &old_level);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return mcp_tool_reply_error(result, "pin_not_output_capable", "GPIO cannot be driven");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "internal", esp_err_to_name(err));
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddNumberToObject(root, "pin", pin);
    cJSON_AddNumberToObject(root, "active_level", active);
    cJSON_AddNumberToObject(root, "duration_ms", duration_ms);
    cJSON_AddNumberToObject(root, "old_level", old_level);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t led_set_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    bool on = esp_mcp_property_list_get_property_bool(properties, "on");
    const char *color = esp_mcp_property_list_get_property_string(properties, "color");
    int brightness = esp_mcp_property_list_get_property_int(properties, "brightness");

    esp_err_t err = led_status_set(on, color, brightness);
    if (err == ESP_ERR_INVALID_STATE) {
        return mcp_tool_reply_error(result, "led_not_configured", "WS2812 is not available");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return mcp_tool_reply_error(result, "invalid_args", "color must be #RRGGBB");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "internal", esp_err_to_name(err));
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "on", on);
    cJSON_AddStringToObject(root, "color", color ? color : "#0000FF");
    cJSON_AddNumberToObject(root, "brightness", brightness);
    return mcp_tool_reply(result, root, false);
}

void mcp_gpio_snapshot_fill(cJSON *array)
{
    gpio_guard_list_json(array);
}

void mcp_register_gpio_tools(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }

    esp_mcp_tool_t *tool = esp_mcp_tool_create_ex("gpio_list", "GPIO List",
        "List allowlisted GPIO pins with current level and mode.", gpio_list_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("gpio_read", "GPIO Read",
        "Read an allowlisted GPIO pin. Argument: pin (integer).", gpio_read_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("pin", 0, 48));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("gpio_write", "GPIO Write",
        "Drive an allowlisted GPIO. Arguments: pin (0-48), level (high|low), hold_ms (0-60000, optional auto-revert).",
        gpio_write_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("pin", 0, 48));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create("level", ESP_MCP_PROPERTY_TYPE_STRING));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_int_and_range("hold_ms", 0, 0, 60000));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("gpio_pulse", "GPIO Pulse",
        "Emit a non-blocking pulse on an allowlisted GPIO. Arguments: pin, active_level (high|low), duration_ms (10-5000).",
        gpio_pulse_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("pin", 0, 48));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("active_level", "high"));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("duration_ms", 10, 5000));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("led_set", "LED Set",
        "Control the onboard WS2812. Arguments: on (bool), color (#RRGGBB, optional), brightness (0-100, optional).",
        led_set_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_bool("on", true));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("color", "#0000FF"));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_int_and_range("brightness", 20, 0, 100));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }
}
