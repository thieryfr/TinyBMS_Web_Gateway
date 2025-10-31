#include "config_manager.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void config_manager_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void config_manager_init(void)
{
    (void)s_event_publisher;
    // TODO: Load configuration from NVS or defaults
}
