#include "monitoring.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void monitoring_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void monitoring_init(void)
{
    (void)s_event_publisher;
    // TODO: Initialize monitoring resources and data publishers
}
