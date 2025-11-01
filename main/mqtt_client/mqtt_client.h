#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "event_bus.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x102
#define ESP_ERR_NOT_SUPPORTED 0x103
#endif

struct esp_mqtt_client;
typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

/**
 * @brief Identifiers for high level MQTT client events.
 */
typedef enum {
    MQTT_CLIENT_EVENT_CONNECTED = 0x2000,
    MQTT_CLIENT_EVENT_DISCONNECTED = 0x2001,
    MQTT_CLIENT_EVENT_SUBSCRIBED = 0x2002,
    MQTT_CLIENT_EVENT_PUBLISHED = 0x2003,
    MQTT_CLIENT_EVENT_DATA = 0x2004,
    MQTT_CLIENT_EVENT_ERROR = 0x20FF,
} mqtt_client_event_id_t;

/**
 * @brief Payload passed to the registered MQTT client callback.
 */
typedef struct {
    mqtt_client_event_id_t id;
    const void *payload;
    size_t payload_size;
} mqtt_client_event_t;

typedef void (*mqtt_client_event_cb_t)(const mqtt_client_event_t *event, void *context);

/**
 * @brief Registration parameters for the optional MQTT client callback.
 */
typedef struct {
    mqtt_client_event_cb_t callback;
    void *context;
} mqtt_client_event_listener_t;

/**
 * @brief Register the event bus publisher used to propagate MQTT events.
 */
void mqtt_client_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Initialise the MQTT client module and install the optional listener.
 */
esp_err_t mqtt_client_init(const mqtt_client_event_listener_t *listener);
/**
 * @brief Start the MQTT client connection state machine.
 */
esp_err_t mqtt_client_start(void);
/**
 * @brief Stop the MQTT client and release its runtime resources.
 */
void mqtt_client_stop(void);
/**
 * @brief Thread-safe publish helper delegating to the ESP-IDF MQTT client.
 */
bool mqtt_client_publish(const char *topic,
                         const void *payload,
                         size_t payload_length,
                         int qos,
                         bool retain,
                         TickType_t timeout);

#ifdef __cplusplus
}
#endif
