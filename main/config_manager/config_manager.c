#include "config_manager.h"

#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "app_events.h"

static const char *TAG = "config_manager";

static event_bus_publish_fn_t s_event_publisher = NULL;
static char s_config_json[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
static size_t s_config_length = 0;

static void config_manager_publish_event(app_event_id_t id)
{
    if (s_event_publisher == NULL || s_config_length == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = id,
        .payload = s_config_json,
        .payload_size = s_config_length + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish config event %u", (unsigned)id);
    }
}

static void config_manager_load_defaults(void)
{
    static const char *k_default_config =
        "{\"device\":\"TinyBMS Gateway\",\"version\":1}";
    s_config_length = strlen(k_default_config);
    memcpy(s_config_json, k_default_config, s_config_length + 1);
}

void config_manager_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void config_manager_init(void)
{
    if (s_config_length == 0) {
        config_manager_load_defaults();
    }
}

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_config_length + 1 > buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(buffer, s_config_json, s_config_length + 1);
    if (out_length != NULL) {
        *out_length = s_config_length;
    }
    return ESP_OK;
}

esp_err_t config_manager_set_config_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length == 0) {
        length = strlen(json);
    }

    if (length + 1 > sizeof(s_config_json)) {
        ESP_LOGW(TAG, "Config payload too large: %u bytes", (unsigned)length);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(s_config_json, json, length);
    s_config_json[length] = '\0';
    s_config_length = length;

    config_manager_publish_event(APP_EVENT_ID_CONFIG_UPDATED);
    return ESP_OK;
}
