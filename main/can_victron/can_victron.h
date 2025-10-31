#pragma once

#include "esp_err.h"

#include "event_bus.h"

void can_victron_init(void);
void can_victron_set_event_publisher(event_bus_publish_fn_t publisher);
esp_err_t can_victron_publish_frame(uint32_t can_id,
                                    const uint8_t *data,
                                    size_t length,
                                    const char *description);
