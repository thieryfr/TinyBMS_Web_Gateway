#include "mqtt_client.h"
#include "mqtt_topics.h"

static event_bus_publish_fn_t s_event_publisher = NULL;

void mqtt_client_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void mqtt_client_init(void)
{
    (void)s_event_publisher;
    // TODO: Initialize MQTT client and subscribe/publish to topics defined in mqtt_topics.h
    (void)MQTT_TOPIC_STATUS;
    (void)MQTT_TOPIC_METRICS;
    (void)MQTT_TOPIC_CONFIG;
}
