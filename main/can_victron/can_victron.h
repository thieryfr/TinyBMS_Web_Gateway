#pragma once

#include "event_bus.h"

void can_victron_init(void);
void can_victron_set_event_publisher(event_bus_publish_fn_t publisher);
