#include "history_fs.h"

#include <stdio.h>

#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h"

#include "freertos/FreeRTOS.h"

#include "app_events.h"

static const char *TAG = "history_fs";

static event_bus_publish_fn_t s_event_publisher = NULL;
static bool s_mounted = false;

static void history_fs_publish_event(app_event_id_t id)
{
    if (s_event_publisher == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = id,
        .payload = NULL,
        .payload_size = 0,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(25))) {
        ESP_LOGW(TAG, "Failed to publish history FS event %u", (unsigned)id);
    }
}

void history_fs_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

bool history_fs_is_mounted(void)
{
#if CONFIG_TINYBMS_HISTORY_FS_ENABLE
    return s_mounted;
#else
    return false;
#endif
}

const char *history_fs_mount_point(void)
{
#if CONFIG_TINYBMS_HISTORY_FS_ENABLE
    return CONFIG_TINYBMS_HISTORY_FS_MOUNT_POINT;
#else
    return "";
#endif
}

esp_err_t history_fs_get_usage(size_t *total_bytes, size_t *used_bytes)
{
#if !CONFIG_TINYBMS_HISTORY_FS_ENABLE
    (void)total_bytes;
    (void)used_bytes;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t total = 0;
    size_t used = 0;
    esp_err_t err = esp_littlefs_info(CONFIG_TINYBMS_HISTORY_FS_PARTITION_LABEL, &total, &used);
    if (err != ESP_OK) {
        return err;
    }

    if (total_bytes != NULL) {
        *total_bytes = total;
    }
    if (used_bytes != NULL) {
        *used_bytes = used;
    }
    return ESP_OK;
#endif
}

void history_fs_init(void)
{
#if !CONFIG_TINYBMS_HISTORY_FS_ENABLE
    ESP_LOGI(TAG, "History LittleFS disabled in configuration");
    return;
#else
    if (s_mounted) {
        return;
    }

    esp_vfs_littlefs_conf_t conf = {0};
    conf.base_path = CONFIG_TINYBMS_HISTORY_FS_MOUNT_POINT;
    conf.partition_label = CONFIG_TINYBMS_HISTORY_FS_PARTITION_LABEL;
    conf.format_if_mount_failed = CONFIG_TINYBMS_HISTORY_FS_FORMAT_ON_FAIL;

    ESP_LOGI(TAG, "Mounting LittleFS history partition '%s' at %s",
             conf.partition_label,
             conf.base_path);

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "LittleFS partition '%s' not found", conf.partition_label);
        } else {
            ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(err));
        }
        s_mounted = false;
        history_fs_publish_event(APP_EVENT_ID_STORAGE_HISTORY_UNAVAILABLE);
        return;
    }

    s_mounted = true;
    history_fs_publish_event(APP_EVENT_ID_STORAGE_HISTORY_READY);

    size_t total = 0;
    size_t used = 0;
    if (history_fs_get_usage(&total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "History LittleFS usage: %u / %u bytes", (unsigned)used, (unsigned)total);
    }
#endif
}

