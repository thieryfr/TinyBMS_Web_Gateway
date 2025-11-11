/**
 * @file config_manager_internal.h
 * @brief Internal header for config manager module (shared across split files)
 *
 * This header is used internally by the config manager module components.
 * It contains declarations for functions and data structures that are
 * shared across the split config_manager files.
 */

#ifndef CONFIG_MANAGER_INTERNAL_H
#define CONFIG_MANAGER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "event_bus.h"
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration limits and constants
// ============================================================================

#define CONFIG_MQTT_BROKER_URI_MAX_LEN 128
#define CONFIG_MQTT_USERNAME_MAX_LEN 64
#define CONFIG_MQTT_PASSWORD_MAX_LEN 64
#define CONFIG_WIFI_SSID_MAX_LEN 32
#define CONFIG_WIFI_PASSWORD_MAX_LEN 64
#define CONFIG_HOSTNAME_MAX_LEN 32

#define CONFIG_VOLTAGE_MIN_LIMIT 2.0f   // Volts
#define CONFIG_VOLTAGE_MAX_LIMIT 4.5f   // Volts
#define CONFIG_TEMP_MIN_LIMIT -40.0f    // Celsius
#define CONFIG_TEMP_MAX_LIMIT 85.0f     // Celsius
#define CONFIG_CURRENT_MAX_LIMIT 500.0f // Amperes

#define CONFIG_POLL_INTERVAL_MIN_MS 100
#define CONFIG_POLL_INTERVAL_MAX_MS 10000

#define CONFIG_SOC_MIN_PERCENT 0
#define CONFIG_SOC_MAX_PERCENT 100

#define CONFIG_NVS_NAMESPACE "tinybms_cfg"
#define CONFIG_NVS_MQTT_NAMESPACE "mqtt_cfg"
#define CONFIG_NVS_WIFI_NAMESPACE "wifi_cfg"

// ============================================================================
// External state (from config_manager_core.c)
// ============================================================================

extern tinybms_config_t g_config;
extern SemaphoreHandle_t g_config_mutex;
extern event_bus_publish_fn_t g_config_event_publisher;

// ============================================================================
// Core functions (from config_manager_core.c)
// ============================================================================

/**
 * @brief Take config mutex with timeout
 */
bool config_lock(TickType_t timeout);

/**
 * @brief Release config mutex
 */
void config_unlock(void);

/**
 * @brief Publish CONFIG_UPDATED event
 */
void config_publish_updated_event(void);

// ============================================================================
// Validation functions (from config_manager_validation.c)
// ============================================================================

/**
 * @brief Validate MQTT broker URI format
 *
 * @param uri Broker URI to validate
 * @return ESP_OK if valid, error otherwise
 */
esp_err_t config_validate_mqtt_broker_uri(const char *uri);

/**
 * @brief Validate WiFi SSID
 *
 * @param ssid SSID to validate
 * @return ESP_OK if valid, error otherwise
 */
esp_err_t config_validate_wifi_ssid(const char *ssid);

/**
 * @brief Validate WiFi password
 *
 * @param password Password to validate
 * @return ESP_OK if valid, error otherwise
 */
esp_err_t config_validate_wifi_password(const char *password);

/**
 * @brief Validate voltage limits
 *
 * @param min_voltage Minimum voltage
 * @param max_voltage Maximum voltage
 * @return ESP_OK if valid (min < max, within ranges), error otherwise
 */
esp_err_t config_validate_voltage_limits(float min_voltage, float max_voltage);

/**
 * @brief Validate temperature limits
 *
 * @param min_temp Minimum temperature
 * @param max_temp Maximum temperature
 * @return ESP_OK if valid (min < max, within ranges), error otherwise
 */
esp_err_t config_validate_temperature_limits(float min_temp, float max_temp);

/**
 * @brief Validate current limit
 *
 * @param max_current Maximum current
 * @return ESP_OK if valid (within range), error otherwise
 */
esp_err_t config_validate_current_limit(float max_current);

/**
 * @brief Validate poll interval
 *
 * @param interval_ms Poll interval in milliseconds
 * @return ESP_OK if valid (within range), error otherwise
 */
esp_err_t config_validate_poll_interval(uint16_t interval_ms);

/**
 * @brief Validate SOC percent
 *
 * @param soc_percent SOC percentage (0-100)
 * @return ESP_OK if valid (0-100), error otherwise
 */
esp_err_t config_validate_soc_percent(uint16_t soc_percent);

/**
 * @brief Validate complete configuration
 *
 * Validates all fields in config struct for consistency and ranges.
 *
 * @param config Configuration to validate
 * @return ESP_OK if all valid, error otherwise
 */
esp_err_t config_validate_complete(const tinybms_config_t *config);

// ============================================================================
// JSON functions (from config_manager_json.c)
// ============================================================================

/**
 * @brief Parse MQTT section from JSON
 *
 * @param json_root cJSON root object
 * @param config Configuration to update
 * @return ESP_OK if parsed successfully, error otherwise
 */
esp_err_t config_parse_mqtt_section(void *json_root, tinybms_config_t *config);

/**
 * @brief Parse WiFi section from JSON
 *
 * @param json_root cJSON root object
 * @param config Configuration to update
 * @return ESP_OK if parsed successfully, error otherwise
 */
esp_err_t config_parse_wifi_section(void *json_root, tinybms_config_t *config);

/**
 * @brief Parse alerts section from JSON
 *
 * @param json_root cJSON root object
 * @param config Configuration to update
 * @return ESP_OK if parsed successfully, error otherwise
 */
esp_err_t config_parse_alerts_section(void *json_root, tinybms_config_t *config);

/**
 * @brief Parse BMS section from JSON
 *
 * @param json_root cJSON root object
 * @param config Configuration to update
 * @return ESP_OK if parsed successfully, error otherwise
 */
esp_err_t config_parse_bms_section(void *json_root, tinybms_config_t *config);

/**
 * @brief Generate MQTT section in JSON
 *
 * @param json_root cJSON root object to add to
 * @param config Configuration source
 * @return ESP_OK if generated successfully, error otherwise
 */
esp_err_t config_generate_mqtt_json(void *json_root, const tinybms_config_t *config);

/**
 * @brief Generate WiFi section in JSON
 *
 * @param json_root cJSON root object to add to
 * @param config Configuration source
 * @return ESP_OK if generated successfully, error otherwise
 */
esp_err_t config_generate_wifi_json(void *json_root, const tinybms_config_t *config);

/**
 * @brief Generate alerts section in JSON
 *
 * @param json_root cJSON root object to add to
 * @param config Configuration source
 * @return ESP_OK if generated successfully, error otherwise
 */
esp_err_t config_generate_alerts_json(void *json_root, const tinybms_config_t *config);

// ============================================================================
// MQTT config functions (from config_manager_mqtt.c)
// ============================================================================

/**
 * @brief Get MQTT configuration
 *
 * @param out_config Output buffer for MQTT config
 * @return ESP_OK on success
 */
esp_err_t config_get_mqtt_config(mqtt_client_config_t *out_config);

/**
 * @brief Set MQTT configuration
 *
 * @param mqtt_config MQTT config to set
 * @return ESP_OK if valid and saved, error otherwise
 */
esp_err_t config_set_mqtt_config(const mqtt_client_config_t *mqtt_config);

/**
 * @brief Validate MQTT configuration
 *
 * @param mqtt_config MQTT config to validate
 * @return ESP_OK if valid, error otherwise
 */
esp_err_t config_validate_mqtt_config(const mqtt_client_config_t *mqtt_config);

// ============================================================================
// Network config functions (from config_manager_network.c)
// ============================================================================

/**
 * @brief Get WiFi configuration
 *
 * @param out_ssid Output buffer for SSID
 * @param ssid_size SSID buffer size
 * @param out_password Output buffer for password
 * @param password_size Password buffer size
 * @return ESP_OK on success
 */
esp_err_t config_get_wifi_config(char *out_ssid, size_t ssid_size,
                                 char *out_password, size_t password_size);

/**
 * @brief Set WiFi configuration
 *
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK if valid and saved, error otherwise
 */
esp_err_t config_set_wifi_config(const char *ssid, const char *password);

/**
 * @brief Validate WiFi configuration
 *
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK if valid, error otherwise
 */
esp_err_t config_validate_wifi_config(const char *ssid, const char *password);

/**
 * @brief Get hostname configuration
 *
 * @param out_hostname Output buffer for hostname
 * @param hostname_size Hostname buffer size
 * @return ESP_OK on success
 */
esp_err_t config_get_hostname(char *out_hostname, size_t hostname_size);

/**
 * @brief Set hostname configuration
 *
 * @param hostname Hostname to set
 * @return ESP_OK if valid and saved, error otherwise
 */
esp_err_t config_set_hostname(const char *hostname);

#ifdef __cplusplus
}
#endif

#endif  // CONFIG_MANAGER_INTERNAL_H
