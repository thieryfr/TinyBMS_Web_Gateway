#include "web_server.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void web_server_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void web_server_init(void)
{
    (void)s_event_publisher;
    // TODO: Start HTTP server and serve dashboard assets
}
