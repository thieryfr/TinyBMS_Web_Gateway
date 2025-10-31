#include "pgn_mapper.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void pgn_mapper_init(void)
{
    (void)s_event_publisher;
    // TODO: Load mapping rules and prepare conversion logic between TinyBMS and Victron PGNs
}
