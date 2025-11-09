#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
#define ESP_ERR_INVALID_ARG 0x104
#endif

struct esp_mqtt_client;
typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

/**
 * @brief Maximum length (including the terminating null byte) accepted for the
 *        MQTT broker URI when configuring the client.
 */
#define MQTT_CLIENT_MAX_URI_LENGTH 128U

/**
 * @brief Maximum length (including the terminating null byte) accepted for the
 *        MQTT username and password fields.
 */
#define MQTT_CLIENT_MAX_CREDENTIAL_LENGTH 64U

/**
 * @brief Maximum length (including the terminating null byte) accepted for TLS
 *        related file paths when configuring the client.
 */
#define MQTT_CLIENT_MAX_TLS_PATH_LENGTH 128U

/**
 * @brief MQTT configuration persisted by the configuration manager.
 */
typedef struct {
    char broker_uri[MQTT_CLIENT_MAX_URI_LENGTH]; /**< URI of the MQTT broker. */
    char username[MQTT_CLIENT_MAX_CREDENTIAL_LENGTH]; /**< Optional username. */
    char password[MQTT_CLIENT_MAX_CREDENTIAL_LENGTH]; /**< Optional password. */
    char client_cert_path[MQTT_CLIENT_MAX_TLS_PATH_LENGTH]; /**< Optional client certificate path. */
    char ca_cert_path[MQTT_CLIENT_MAX_TLS_PATH_LENGTH];      /**< Optional CA certificate path. */
    uint16_t keepalive_seconds; /**< Keepalive interval negotiated with the broker. */
    uint8_t default_qos;        /**< Default QoS level used for publications. */
    bool retain_enabled;        /**< Set to true to retain status publications. */
    bool verify_hostname;       /**< When true, enforce broker hostname validation. */
} mqtt_client_config_t;

/**
 * @brief Runtime state of the lightweight MQTT client wrapper.
 */
typedef struct {
    bool lock_created;             /**< True when the internal mutex has been created. */
    bool initialised;              /**< True after ::mqtt_client_init succeeds. */
    bool started;                  /**< True after ::mqtt_client_start succeeds. */
    bool client_handle_created;    /**< True when an ESP-IDF client handle is present. */
    bool listener_registered;      /**< True when an optional listener callback is configured. */
    bool event_publisher_registered; /**< True when an event publisher callback is registered. */
} mqtt_client_state_t;

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
 * @brief Apply a new runtime configuration to the MQTT client handle.
 */
esp_err_t mqtt_client_apply_configuration(const mqtt_client_config_t *config);
/**
 * @brief Start the MQTT client connection state machine.
 */
esp_err_t mqtt_client_start(void);
/**
 * @brief Stop the MQTT client and release its runtime resources.
 */
void mqtt_client_stop(void);

/**
 * @brief Alias for mqtt_client_stop() for consistency with other modules.
 */
static inline void mqtt_client_deinit(void) { mqtt_client_stop(); }
/**
 * @brief Thread-safe publish helper delegating to the ESP-IDF MQTT client.
 */
bool mqtt_client_publish(const char *topic,
                         const void *payload,
                         size_t payload_length,
                         int qos,
                         bool retain,
                         TickType_t timeout);

/**
 * @brief Attempt a one-shot MQTT connection using the provided configuration.
 *
 * @param[in]  config        Configuration describing the broker connection.
 * @param[in]  timeout       Maximum time to wait for a connection result.
 * @param[out] connected     Set to true when the connection succeeds.
 * @param[out] error_message Optional buffer receiving a human-readable status.
 * @param[in]  error_size    Size of @p error_message in bytes.
 *
 * @return ESP_OK on successful connection, ESP_ERR_TIMEOUT on timeout,
 *         ESP_ERR_NOT_SUPPORTED when not available on the current platform, or
 *         another esp_err_t value describing the failure.
 */
esp_err_t mqtt_client_test_connection(const mqtt_client_config_t *config,
                                      TickType_t timeout,
                                      bool *connected,
                                      char *error_message,
                                      size_t error_size);

/**
 * @brief Copy the internal MQTT client state into @p state for diagnostics and testing.
 */
void mqtt_client_get_state(mqtt_client_state_t *state);

#ifdef __cplusplus
}
#endif
