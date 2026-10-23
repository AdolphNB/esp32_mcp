#include "mcp_tools.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mcp_resource.h"

static esp_err_t read_device_info(const char *uri, char **out_mime, char **out_text,
                                  char **out_blob, void *ctx)
{
    (void)uri;
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    mcp_device_info_fill(root);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    *out_mime = strdup("application/json");
    if (!*out_mime) {
        free(text);
        return ESP_ERR_NO_MEM;
    }
    *out_text = text;
    *out_blob = NULL;
    return ESP_OK;
}

static esp_err_t read_gpio(const char *uri, char **out_mime, char **out_text,
                           char **out_blob, void *ctx)
{
    (void)uri;
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *pins = cJSON_AddArrayToObject(root, "pins");
    mcp_gpio_snapshot_fill(pins);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    *out_mime = strdup("application/json");
    if (!*out_mime) {
        free(text);
        return ESP_ERR_NO_MEM;
    }
    *out_text = text;
    *out_blob = NULL;
    return ESP_OK;
}

void mcp_register_resources(esp_mcp_t *mcp)
{
    if (!mcp) {
        return;
    }

    esp_mcp_resource_t *res = esp_mcp_resource_create(
        "device://info", "device_info", "Device Info",
        "Live device metadata as JSON", "application/json",
        read_device_info, NULL);
    if (res) {
        esp_mcp_resource_set_annotations(res, "[\"assistant\"]", 0.5, NULL);
        esp_mcp_add_resource(mcp, res);
    }

    res = esp_mcp_resource_create(
        "device://gpio", "gpio_snapshot", "GPIO Snapshot",
        "Allowlisted GPIO pins with current level", "application/json",
        read_gpio, NULL);
    if (res) {
        esp_mcp_resource_set_annotations(res, "[\"assistant\"]", 0.5, NULL);
        esp_mcp_add_resource(mcp, res);
    }
}
