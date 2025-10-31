#pragma once

#include "event_bus.h"

void web_server_init(void);
void web_server_set_event_publisher(event_bus_publish_fn_t publisher);
