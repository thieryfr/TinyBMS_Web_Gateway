#pragma once

/**
 * MQTT topic layout (all topics prefixed by the device identifier).
 *
 * Recommended QoS / retain combinations:
 *  - Status snapshots: QoS 1, retained to expose the latest state to new subscribers.
 *  - Metrics streams: QoS 0, not retained to avoid stale telemetry.
 *  - CAN frames: QoS 0, not retained due to high frequency updates.
 *  - Configuration acknowledgements: QoS 1, not retained to keep the channel transient.
 */
#define MQTT_TOPIC_FMT_STATUS "bms/%s/status"
#define MQTT_TOPIC_FMT_METRICS "bms/%s/metrics"
#define MQTT_TOPIC_FMT_CAN_STREAM "bms/%s/can/%s"
#define MQTT_TOPIC_FMT_CAN_SUBTREE "bms/%s/can/#"
#define MQTT_TOPIC_FMT_CONFIG "bms/%s/config"

#define MQTT_TOPIC_STATUS_QOS 1
#define MQTT_TOPIC_STATUS_RETAIN true

#define MQTT_TOPIC_METRICS_QOS 0
#define MQTT_TOPIC_METRICS_RETAIN false

#define MQTT_TOPIC_CAN_QOS 0
#define MQTT_TOPIC_CAN_RETAIN false

#define MQTT_TOPIC_CONFIG_QOS 1
#define MQTT_TOPIC_CONFIG_RETAIN false
