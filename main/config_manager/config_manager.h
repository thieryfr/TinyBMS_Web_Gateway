#pragma once

#include <stddef.h>

#include "esp_err.h"

#include "event_bus.h"

void config_manager_init(void);
void config_manager_set_event_publisher(event_bus_publish_fn_t publisher);

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t config_manager_set_config_json(const char *json, size_t length);

#define CONFIG_MANAGER_MAX_CONFIG_SIZE 2048

