#pragma once

#include "esp_err.h"

typedef void (*event_bus_callback_t)(void *context);

esp_err_t event_bus_init(void);
esp_err_t event_bus_register(event_bus_callback_t cb, void *context);
esp_err_t event_bus_post(event_bus_callback_t cb, void *context);
