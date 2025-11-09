#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_manager.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool client_started;
    bool connected;
    bool wifi_connected;
    uint32_t reconnect_count;
    uint32_t disconnect_count;
    uint32_t error_count;
    mqtt_client_event_id_t last_event;
    uint64_t last_event_timestamp_ms;
    char broker_uri[MQTT_CLIENT_MAX_URI_LENGTH];
    char status_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char metrics_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char config_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_raw_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_decoded_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_ready_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char last_error[96];
} mqtt_gateway_status_t;

/**
 * @brief Initialise the MQTT gateway responsible for bridging events to MQTT topics.
 */
void mqtt_gateway_init(void);

/**
 * @brief Deinitialize the MQTT gateway and free resources.
 */
void mqtt_gateway_deinit(void);

/**
 * @brief Retrieve the listener definition registered with the MQTT client module.
 */
const mqtt_client_event_listener_t *mqtt_gateway_get_event_listener(void);

/**
 * @brief Copy the current MQTT gateway runtime status into @p status.
 */
void mqtt_gateway_get_status(mqtt_gateway_status_t *status);

#ifdef __cplusplus
}
#endif

