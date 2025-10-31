#pragma once

#include "event_bus.h"

void uart_bms_init(void);
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);
