#include "event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_event_queue = NULL;

void event_bus_init(void)
{
    if (s_event_queue == NULL) {
        s_event_queue = xQueueCreate(10, sizeof(uint32_t));
    }
}
