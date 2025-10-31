#include "can_victron.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void can_victron_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void can_victron_init(void)
{
    (void)s_event_publisher;
    // TODO: Initialize CAN peripheral and configure Victron-specific settings
}
