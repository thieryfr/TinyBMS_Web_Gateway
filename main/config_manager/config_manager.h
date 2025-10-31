#pragma once

#include "event_bus.h"

void config_manager_init(void);
void config_manager_set_event_publisher(event_bus_publish_fn_t publisher);
