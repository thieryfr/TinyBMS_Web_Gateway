#include "mqtt_client.h"
#include "mqtt_topics.h"

void mqtt_client_init(void)
{
    // TODO: Initialize MQTT client and subscribe/publish to topics defined in mqtt_topics.h
    (void)MQTT_TOPIC_STATUS;
    (void)MQTT_TOPIC_METRICS;
    (void)MQTT_TOPIC_CONFIG;
}
