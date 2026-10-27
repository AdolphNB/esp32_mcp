#include "mcp_tools.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "driver/i2c_master.h"
#include "mcp_tools_common.h"
#include "rate_limit.h"
#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

static bool reserved_i2c_addr(int addr)
{
    switch (addr) {
    case 0x23: /* CH422G OC */
    case 0x24: /* CH422G mode */
    case 0x26: /* CH422G input */
    case 0x38: /* CH422G IO */
    case 0x51: /* PCF85063 RTC */
    case 0x5D: /* GT911 */
    case 0x14: /* GT911 alternate */
        return true;
    default:
        return false;
    }
}

static void add_io_json(cJSON *root, uint8_t di, uint8_t do_bits)
{
    cJSON *dij = cJSON_AddObjectToObject(root, "di");
    cJSON_AddBoolToObject(dij, "di0", (di & 0x01) != 0);
    cJSON_AddBoolToObject(dij, "di1", (di & 0x02) != 0);
    cJSON *doj = cJSON_AddObjectToObject(root, "do");
    for (int i = 0; i < BOARD_DO_COUNT; i++) {
        char name[8];
        snprintf(name, sizeof(name), "do%d", i);
        cJSON_AddBoolToObject(doj, name, (do_bits & (1u << i)) != 0);
    }
}

static esp_err_t io_status_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    const board_desc_t *board = board_get();
    if (!board || !board->has_isolated_io) {
        return mcp_tool_reply_error(result, "not_supported", "this board has no isolated IO");
    }
    uint8_t di = 0, do_bits = 0;
    esp_err_t err = board_io_get(&di, &do_bits);
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "io_error", esp_err_to_name(err));
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "writable", APP_DISPLAY_ALLOW_DO_WRITE);
    add_io_json(root, di, do_bits);
    cJSON *i2c = cJSON_AddObjectToObject(root, "i2c");
    cJSON_AddNumberToObject(i2c, "sda", board->i2c_sda);
    cJSON_AddNumberToObject(i2c, "scl", board->i2c_scl);
    cJSON *rs485 = cJSON_AddObjectToObject(root, "rs485");
    cJSON_AddNumberToObject(rs485, "tx", board->rs485_tx);
    cJSON_AddNumberToObject(rs485, "rx", board->rs485_rx);
    cJSON *can = cJSON_AddObjectToObject(root, "can");
    cJSON_AddNumberToObject(can, "tx", board->can_tx);
    cJSON_AddNumberToObject(can, "rx", board->can_rx);
    return mcp_tool_reply(result, root, false);
}

static esp_err_t io_read_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    uint8_t di = 0;
    esp_err_t err = board_io_read(&di);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return mcp_tool_reply_error(result, "not_supported", "this board has no isolated DI");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "io_error", esp_err_to_name(err));
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    add_io_json(root, di, 0);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "do");
    return mcp_tool_reply(result, root, false);
}

static esp_err_t io_write_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
#if !APP_DISPLAY_ALLOW_DO_WRITE
    return mcp_tool_reply_error(result, "disabled",
                                "DO writes are disabled; enable APP_DISPLAY_ALLOW_DO_WRITE");
#else
    const char *confirm = esp_mcp_property_list_get_property_string(properties, "confirm");
    if (!confirm || strcmp(confirm, "IO") != 0) {
        return mcp_tool_reply_error(result, "confirm_required", "confirm must be \"IO\"");
    }
    int channel = esp_mcp_property_list_get_property_int(properties, "channel");
    bool on = esp_mcp_property_list_get_property_bool(properties, "on");
    if (channel < 0 || channel >= BOARD_DO_COUNT) {
        return mcp_tool_reply_error(result, "invalid_args", "channel must be 0..3");
    }
    uint8_t mask = (uint8_t)(1u << channel);
    esp_err_t err = board_io_write(on ? mask : 0, mask);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return mcp_tool_reply_error(result, "not_supported", "this board has no isolated DO");
    }
    if (err != ESP_OK) {
        return mcp_tool_reply_error(result, "io_error", esp_err_to_name(err));
    }
    uint8_t di = 0, do_bits = 0;
    board_io_get(&di, &do_bits);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "channel", channel);
    cJSON_AddBoolToObject(root, "on", on);
    add_io_json(root, di, do_bits);
    return mcp_tool_reply(result, root, false);
#endif
}

static esp_err_t i2c_scan_cb(const esp_mcp_property_list_t *properties, esp_mcp_tool_result_t *result)
{
    (void)properties;
    if (!rate_limit_allow()) {
        return mcp_tool_reply_error(result, "rate_limited", "too many calls");
    }
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus) {
        return mcp_tool_reply_error(result, "not_supported", "I2C bus is not initialized");
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return mcp_tool_reply_error(result, "internal", "out of memory");
    }
    cJSON *found = cJSON_AddArrayToObject(root, "found");
    for (int addr = 0x08; addr <= 0x77; addr++) {
        if (reserved_i2c_addr(addr)) {
            continue;
        }
        if (i2c_master_probe(bus, (uint16_t)addr, 30) == ESP_OK) {
            cJSON_AddItemToArray(found, cJSON_CreateNumber(addr));
        }
    }
    cJSON_AddNumberToObject(root, "sda", board_get() ? board_get()->i2c_sda : -1);
    cJSON_AddNumberToObject(root, "scl", board_get() ? board_get()->i2c_scl : -1);
    cJSON *reserved = cJSON_AddArrayToObject(root, "reserved");
    const int reserved_addrs[] = {0x23, 0x24, 0x26, 0x38, 0x51, 0x5D, 0x14};
    for (size_t i = 0; i < sizeof(reserved_addrs) / sizeof(reserved_addrs[0]); i++) {
        cJSON_AddItemToArray(reserved, cJSON_CreateNumber(reserved_addrs[i]));
    }
    return mcp_tool_reply(result, root, false);
}

void mcp_register_io_tools(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }

    esp_mcp_tool_t *tool = esp_mcp_tool_create_ex("io_status", "IO Status",
        "Show isolated DI/DO state and I2C/RS485/CAN pin information.", io_status_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("io_read", "Read Isolated Inputs",
        "Read isolated DI0/DI1 (5-36V).", io_read_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("io_write", "Write Isolated Output",
        "Set an isolated DO channel. Args: channel(0-3), on(bool), confirm=\"IO\". "
        "Disabled unless APP_DISPLAY_ALLOW_DO_WRITE is enabled.", io_write_cb);
    if (tool) {
        esp_mcp_tool_add_property(tool,
            esp_mcp_property_create_with_int_and_range("channel", 0, 0, BOARD_DO_COUNT - 1));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_bool("on", false));
        esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("confirm", ""));
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":false,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }

    tool = esp_mcp_tool_create_ex("i2c_scan", "Scan I2C Bus",
        "Scan the external I2C bus (SDA=8, SCL=9) and skip reserved devices.", i2c_scan_cb);
    if (tool) {
        esp_mcp_tool_set_annotations_json(tool,
            "{\"readOnlyHint\":true,\"idempotentHint\":true,\"openWorldHint\":false}");
        esp_mcp_add_tool(mcp, tool);
    }
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */
