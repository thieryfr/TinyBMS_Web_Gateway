#pragma once

/**
 * @file config_manager.h
 * @brief Gateway configuration management module
 *
 * Manages device settings, WiFi configuration, UART/CAN parameters,
 * and MQTT connectivity settings with NVS persistence.
 *
 * @section config_thread_safety Thread Safety
 *
 * The configuration manager uses an internal mutex (s_config_mutex) to protect
 * configuration state modifications. All setter functions are thread-safe.
 *
 * **Thread-Safe Functions** (mutex-protected):
 * - config_manager_set_uart_poll_interval_ms()
 * - All future setter functions (TODO: add mutex protection)
 * - config_manager_get_*() functions (return thread-safe snapshots)
 *
 * **Initialization**: Must call config_manager_init() before other functions.
 *
 * Getter functions take the internal mutex while duplicating their respective
 * configuration structures and return pointers to immutable snapshots.
 *
 * @section config_usage Usage Example
 * @code
 * // Initialize module
 * config_manager_init();
 *
 * // Read configuration (thread-safe for read-only access)
 * uint32_t interval = config_manager_get_uart_poll_interval_ms();
 *
 * // Modify configuration (thread-safe)
 * esp_err_t err = config_manager_set_uart_poll_interval_ms(500);
 * @endcode
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "event_bus.h"
#include "mqtt_client.h"

#define CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH 64

#define CONFIG_MANAGER_WIFI_SSID_MAX_LENGTH      32
#define CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH  64
#define CONFIG_MANAGER_WIFI_HOSTNAME_MAX_LENGTH  32

#define CONFIG_MANAGER_CAN_HANDSHAKE_MAX_LENGTH  8
#define CONFIG_MANAGER_CAN_STRING_MAX_LENGTH     32
#define CONFIG_MANAGER_CAN_SERIAL_MAX_LENGTH     32

#define CONFIG_MANAGER_SECRET_MASK "********"

typedef struct {
    char name[CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH];
} config_manager_device_settings_t;

typedef struct {
    int tx_gpio;
    int rx_gpio;
} config_manager_uart_pins_t;

typedef struct {
    struct {
        char ssid[CONFIG_MANAGER_WIFI_SSID_MAX_LENGTH];
        char password[CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH];
        char hostname[CONFIG_MANAGER_WIFI_HOSTNAME_MAX_LENGTH];
        uint8_t max_retry;
    } sta;
    struct {
        char ssid[CONFIG_MANAGER_WIFI_SSID_MAX_LENGTH];
        char password[CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH];
        uint8_t channel;
        uint8_t max_clients;
    } ap;
} config_manager_wifi_settings_t;

typedef struct {
    struct {
        int tx_gpio;
        int rx_gpio;
    } twai;
    struct {
        uint32_t interval_ms;
        uint32_t timeout_ms;
        uint32_t retry_ms;
    } keepalive;
    struct {
        uint32_t period_ms;
    } publisher;
    struct {
        char handshake_ascii[CONFIG_MANAGER_CAN_HANDSHAKE_MAX_LENGTH];
        char manufacturer[CONFIG_MANAGER_CAN_STRING_MAX_LENGTH];
        char battery_name[CONFIG_MANAGER_CAN_STRING_MAX_LENGTH];
        char battery_family[CONFIG_MANAGER_CAN_STRING_MAX_LENGTH];
        char serial_number[CONFIG_MANAGER_CAN_SERIAL_MAX_LENGTH];
    } identity;
} config_manager_can_settings_t;

void config_manager_init(void);
void config_manager_deinit(void);
void config_manager_set_event_publisher(event_bus_publish_fn_t publisher);

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t config_manager_set_config_json(const char *json, size_t length);
esp_err_t config_manager_get_registers_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t config_manager_apply_register_update_json(const char *json, size_t length);

const config_manager_device_settings_t *config_manager_get_device_settings(void);
const char *config_manager_get_device_name(void);

uint32_t config_manager_get_uart_poll_interval_ms(void);
esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms);

const config_manager_uart_pins_t *config_manager_get_uart_pins(void);

const mqtt_client_config_t *config_manager_get_mqtt_client_config(void);
esp_err_t config_manager_set_mqtt_client_config(const mqtt_client_config_t *config);

#define CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH 96

typedef struct {
    char status[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char metrics[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char config[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_raw[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_decoded[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_ready[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
} config_manager_mqtt_topics_t;

const config_manager_mqtt_topics_t *config_manager_get_mqtt_topics(void);
esp_err_t config_manager_set_mqtt_topics(const config_manager_mqtt_topics_t *topics);

const config_manager_wifi_settings_t *config_manager_get_wifi_settings(void);
const config_manager_can_settings_t *config_manager_get_can_settings(void);

const char *config_manager_mask_secret(const char *value);

#define CONFIG_MANAGER_MAX_CONFIG_SIZE 2048
#define CONFIG_MANAGER_MAX_REGISTERS_JSON 4096

