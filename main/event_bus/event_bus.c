#include "event_bus.h"

esp_err_t event_bus_init(void)
{
    return ESP_OK;
}

esp_err_t event_bus_register(event_bus_callback_t cb, void *context)
{
    (void)cb;
    (void)context;
    return ESP_OK;
}

esp_err_t event_bus_post(event_bus_callback_t cb, void *context)
{
    if (cb) {
        cb(context);
    }
    return ESP_OK;
}
