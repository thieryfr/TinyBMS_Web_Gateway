#pragma once

#include "event_bus.h"

void monitoring_init(void);
void monitoring_set_event_publisher(event_bus_publish_fn_t publisher);
