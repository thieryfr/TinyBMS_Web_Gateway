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
 * configuration state. All public getters and setters acquire this mutex to
 * guarantee that callers observe a consistent snapshot even when multiple tasks
 * read and mutate the configuration concurrently.
 *
 * **Initialization**: Must call config_manager_init() before other functions.
 *
 * @section config_usage Usage Example
 * @code
 * config_manager_init();
 *
 * // Read configuration (thread-safe for read-only access)
 * uint32_t interval = config_manager_get_uart_poll_interval_ms();
 *
 * mqtt_client_config_t mqtt_cfg;
 * if (config_manager_get_mqtt_client_config(&mqtt_cfg) == ESP_OK) {
 *     // use mqtt_cfg
 * }
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
void config_manager_set_event_publisher(event_bus_publish_fn_t publisher);

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t config_manager_set_config_json(const char *json, size_t length);
esp_err_t config_manager_get_registers_json(char *buffer, size_t buffer_size, size_t *out_length);
esp_err_t config_manager_apply_register_update_json(const char *json, size_t length);

esp_err_t config_manager_get_device_settings(config_manager_device_settings_t *out);
esp_err_t config_manager_get_device_name(char *buffer, size_t buffer_size);

uint32_t config_manager_get_uart_poll_interval_ms(void);
esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms);

esp_err_t config_manager_get_uart_pins(config_manager_uart_pins_t *out);

esp_err_t config_manager_get_mqtt_client_config(mqtt_client_config_t *out);
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

esp_err_t config_manager_get_mqtt_topics(config_manager_mqtt_topics_t *out);
esp_err_t config_manager_set_mqtt_topics(const config_manager_mqtt_topics_t *topics);

esp_err_t config_manager_get_wifi_settings(config_manager_wifi_settings_t *out);
esp_err_t config_manager_get_can_settings(config_manager_can_settings_t *out);

#define CONFIG_MANAGER_MAX_CONFIG_SIZE 2048
#define CONFIG_MANAGER_MAX_REGISTERS_JSON 4096

