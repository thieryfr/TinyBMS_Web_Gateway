#pragma once

#include "event_bus.h"

void mqtt_client_init(void);
void mqtt_client_set_event_publisher(event_bus_publish_fn_t publisher);
