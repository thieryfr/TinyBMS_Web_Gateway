#pragma once

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
