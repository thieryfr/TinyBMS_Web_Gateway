#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the LittleFS partition that stores telemetry archives.
 */
void history_fs_init(void);

/**
 * @brief Deinitialize the history filesystem and unmount the partition.
 */
void history_fs_deinit(void);

/**
 * @brief Provide the event publisher used to report history storage state changes.
 */
void history_fs_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Returns true when the history LittleFS partition is mounted and available.
 */
bool history_fs_is_mounted(void);

/**
 * @brief Obtain the total and used capacity reported by the LittleFS partition.
 */
esp_err_t history_fs_get_usage(size_t *total_bytes, size_t *used_bytes);

/**
 * @brief Return the base mount path used for the history LittleFS filesystem.
 */
const char *history_fs_mount_point(void);

#ifdef __cplusplus
}
#endif

