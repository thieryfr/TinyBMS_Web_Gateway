#pragma once

#include "event_bus.h"

void pgn_mapper_init(void);
void pgn_mapper_deinit(void);
void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher);
