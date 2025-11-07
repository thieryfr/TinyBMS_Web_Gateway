#pragma once

/**
 * @file monitoring.h
 * @brief System telemetry aggregation and history tracking module
 *
 * Aggregates BMS data, maintains historical samples, and generates
 * JSON snapshots for web server and MQTT publication.
 *
 * @section monitoring_thread_safety Thread Safety
 *
 * The monitoring module uses an internal mutex (s_monitoring_mutex) to protect
 * all shared state including BMS data snapshots and history buffers.
 *
 * **Protected Resources**:
 * - s_latest_bms - Latest BMS data snapshot
 * - s_has_latest_bms - Data validity flag
 * - s_history[] - Circular buffer of historical samples (512 entries)
 * - s_history_head - History buffer write position
 * - s_history_count - Number of valid history entries
 *
 * **Thread-Safe Functions** (all public functions are mutex-protected):
 * - monitoring_get_status_json() - Reads latest BMS data (mutex-protected copy)
 * - monitoring_get_history_json() - Reads history buffer (mutex-protected)
 * - Internal: monitoring_on_bms_update() - Updates BMS data (mutex-protected)
 * - Internal: monitoring_history_push() - Adds history sample (mutex-protected)
 *
 * **Concurrency Pattern**:
 * The module handles concurrent access from:
 * - UART BMS task (writes latest data)
 * - Web server task (reads for /api/status)
 * - MQTT publisher task (reads for telemetry)
 *
 * @note All mutex operations use 100ms timeout to prevent deadlocks.
 *       Failed mutex acquisition is logged but does not block callers.
 *
 * @section monitoring_usage Usage Example
 * @code
 * // Initialize module
 * monitoring_init();
 * monitoring_set_event_publisher(my_publisher);
 *
 * // Read current status (thread-safe)
 * char json[2048];
 * size_t len;
 * esp_err_t err = monitoring_get_status_json(json, sizeof(json), &len);
 *
 * // Read history (thread-safe)
 * err = monitoring_get_history_json(100, json, sizeof(json), &len);
 * @endcode
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "event_bus.h"

#define MONITORING_SNAPSHOT_MAX_SIZE 2048U

void monitoring_init(void);
void monitoring_set_event_publisher(event_bus_publish_fn_t publisher);

esp_err_t monitoring_get_status_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t monitoring_publish_telemetry_snapshot(void);
esp_err_t monitoring_get_history_json(size_t limit, char *buffer, size_t buffer_size, size_t *out_length);
