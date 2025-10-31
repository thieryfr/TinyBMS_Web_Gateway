#include "uart_bms.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void uart_bms_init(void)
{
    (void)s_event_publisher;
    // TODO: Configure UART driver and start TinyBMS polling task
}
