#pragma once

/**
 * @file alert_manager.h
 * @brief Alert and notification management system for TinyBMS-GW
 *
 * Monitors TinyBMS telemetry and events, triggers configurable alerts,
 * maintains alert history, and publishes notifications via MQTT/WebSocket.
 *
 * Features:
 * - Configurable thresholds for temperature, voltage, current, SOC, cell imbalance
 * - TinyBMS event monitoring (Faults, Warnings, Info messages)
 * - TinyBMS online status tracking (Charging, Discharging, Idle, Fault)
 * - Anti-bounce delay to prevent alert spam
 * - Alert history with circular buffer (last 100 alerts)
 * - Severity levels: INFO, WARNING, CRITICAL
 * - Alert acknowledgement system
 * - MQTT and WebSocket publishing
 * - NVS persistence for configuration
 *
 * @section alert_usage Usage Example
 * @code
 * // Initialize module
 * alert_manager_init();
 * alert_manager_set_event_publisher(event_bus_get_publish_hook());
 *
 * // Configure custom thresholds
 * alert_config_t config = {0};
 * alert_manager_get_config(&config);
 * config.temperature_max_c = 45.0f;
 * config.temperature_min_c = 5.0f;
 * alert_manager_set_config(&config);
 *
 * // Retrieve active alerts
 * alert_entry_t alerts[ALERT_MANAGER_MAX_ACTIVE_ALERTS];
 * size_t count = 0;
 * alert_manager_get_active_alerts(alerts, ALERT_MANAGER_MAX_ACTIVE_ALERTS, &count);
 * @endcode
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants
// =============================================================================

#define ALERT_MANAGER_MAX_ACTIVE_ALERTS  32   /**< Maximum concurrent active alerts */
#define ALERT_MANAGER_MAX_HISTORY        100  /**< Alert history circular buffer size */
#define ALERT_MANAGER_MESSAGE_MAX_LENGTH 128  /**< Max alert message string length */

/**
 * @brief Default anti-bounce delay in seconds.
 * Prevents alert retriggering within this time window.
 */
#define ALERT_MANAGER_DEFAULT_DEBOUNCE_SEC 10

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @brief Alert severity levels
 */
typedef enum {
    ALERT_SEVERITY_INFO     = 0,  /**< Informational (e.g. charging started) */
    ALERT_SEVERITY_WARNING  = 1,  /**< Warning condition (e.g. high temperature) */
    ALERT_SEVERITY_CRITICAL = 2,  /**< Critical fault (e.g. overvoltage cutoff) */
} alert_severity_t;

/**
 * @brief Alert type identifiers
 */
typedef enum {
    // Threshold-based alerts
    ALERT_TYPE_TEMPERATURE_HIGH      = 1,
    ALERT_TYPE_TEMPERATURE_LOW       = 2,
    ALERT_TYPE_CELL_VOLTAGE_HIGH     = 3,
    ALERT_TYPE_CELL_VOLTAGE_LOW      = 4,
    ALERT_TYPE_PACK_VOLTAGE_HIGH     = 5,
    ALERT_TYPE_PACK_VOLTAGE_LOW      = 6,
    ALERT_TYPE_CURRENT_DISCHARGE_HIGH = 7,
    ALERT_TYPE_CURRENT_CHARGE_HIGH   = 8,
    ALERT_TYPE_SOC_LOW               = 9,
    ALERT_TYPE_SOC_HIGH              = 10,
    ALERT_TYPE_CELL_IMBALANCE_HIGH   = 11,

    // TinyBMS online status changes
    ALERT_TYPE_STATUS_CHARGING       = 20,  /**< Status changed to Charging (0x91) */
    ALERT_TYPE_STATUS_FULLY_CHARGED  = 21,  /**< Status changed to Fully Charged (0x92) */
    ALERT_TYPE_STATUS_DISCHARGING    = 22,  /**< Status changed to Discharging (0x93) */
    ALERT_TYPE_STATUS_REGENERATION   = 23,  /**< Status changed to Regeneration (0x96) */
    ALERT_TYPE_STATUS_IDLE           = 24,  /**< Status changed to Idle (0x97) */
    ALERT_TYPE_STATUS_FAULT          = 25,  /**< Status changed to Fault (0x9B) */

    // TinyBMS Events (from documentation Chapter 4)
    // Fault events (0x01-0x30)
    ALERT_TYPE_EVENT_FAULT_BASE      = 100,  // Base offset for fault events
    ALERT_TYPE_EVENT_UNDER_VOLTAGE   = 102,  // 0x02
    ALERT_TYPE_EVENT_OVER_VOLTAGE    = 103,  // 0x03
    ALERT_TYPE_EVENT_OVER_TEMP       = 104,  // 0x04
    ALERT_TYPE_EVENT_DISCHARGE_OC    = 105,  // 0x05
    ALERT_TYPE_EVENT_CHARGE_OC       = 106,  // 0x06
    ALERT_TYPE_EVENT_REGEN_OC        = 107,  // 0x07
    ALERT_TYPE_EVENT_LOW_TEMP        = 110,  // 0x0A
    ALERT_TYPE_EVENT_CHARGER_SWITCH_ERR = 111, // 0x0B
    ALERT_TYPE_EVENT_LOAD_SWITCH_ERR = 112,  // 0x0C
    ALERT_TYPE_EVENT_SINGLE_PORT_ERR = 113,  // 0x0D
    ALERT_TYPE_EVENT_CURRENT_SENSOR_DISC = 114, // 0x0E
    ALERT_TYPE_EVENT_CURRENT_SENSOR_CONN = 115, // 0x0F

    // Warning events (0x31-0x60)
    ALERT_TYPE_EVENT_WARNING_BASE    = 200,  // Base offset for warning events
    ALERT_TYPE_EVENT_FULLY_DISCHARGED = 231, // 0x31
    ALERT_TYPE_EVENT_LOW_TEMP_CHARGE = 237,  // 0x37
    ALERT_TYPE_EVENT_CHARGE_DONE_HIGH = 238, // 0x38
    ALERT_TYPE_EVENT_CHARGE_DONE_LOW = 239,  // 0x39

    // Info events (0x61-0x90)
    ALERT_TYPE_EVENT_INFO_BASE       = 300,  // Base offset for info events
    ALERT_TYPE_EVENT_SYSTEM_STARTED  = 361,  // 0x61
    ALERT_TYPE_EVENT_CHARGING_STARTED = 362, // 0x62
    ALERT_TYPE_EVENT_CHARGING_DONE   = 363,  // 0x63
    ALERT_TYPE_EVENT_CHARGER_CONNECTED = 364, // 0x64
    ALERT_TYPE_EVENT_CHARGER_DISCONNECTED = 365, // 0x65
    // ... (other info events follow same pattern)

} alert_type_t;

/**
 * @brief Alert acknowledgement status
 */
typedef enum {
    ALERT_STATUS_ACTIVE        = 0,  /**< Alert currently active, not acknowledged */
    ALERT_STATUS_ACKNOWLEDGED  = 1,  /**< Alert acknowledged by user */
    ALERT_STATUS_CLEARED       = 2,  /**< Alert condition cleared automatically */
} alert_status_t;

// =============================================================================
// Structures
// =============================================================================

/**
 * @brief Alert configuration (threshold and enable settings)
 */
typedef struct {
    // General settings
    bool     enabled;                /**< Master enable for alert system */
    uint32_t debounce_sec;           /**< Anti-bounce delay in seconds */

    // Temperature thresholds
    bool     temp_high_enabled;
    float    temperature_max_c;      /**< Max temperature threshold (°C) */
    bool     temp_low_enabled;
    float    temperature_min_c;      /**< Min temperature threshold (°C) */

    // Voltage thresholds
    bool     cell_volt_high_enabled;
    uint16_t cell_voltage_max_mv;    /**< Max cell voltage threshold (mV) */
    bool     cell_volt_low_enabled;
    uint16_t cell_voltage_min_mv;    /**< Min cell voltage threshold (mV) */

    bool     pack_volt_high_enabled;
    float    pack_voltage_max_v;     /**< Max pack voltage threshold (V) */
    bool     pack_volt_low_enabled;
    float    pack_voltage_min_v;     /**< Min pack voltage threshold (V) */

    // Current thresholds
    bool     current_discharge_enabled;
    float    discharge_current_max_a; /**< Max discharge current threshold (A) */
    bool     current_charge_enabled;
    float    charge_current_max_a;    /**< Max charge current threshold (A) */

    // SOC thresholds
    bool     soc_low_enabled;
    float    soc_min_pct;            /**< Min SOC threshold (%) */
    bool     soc_high_enabled;
    float    soc_max_pct;            /**< Max SOC threshold (%) */

    // Cell imbalance
    bool     imbalance_enabled;
    uint16_t cell_imbalance_max_mv;  /**< Max cell voltage spread (mV) */

    // TinyBMS event monitoring
    bool     monitor_tinybms_events; /**< Enable TinyBMS event parsing */
    bool     monitor_status_changes; /**< Enable online status change alerts */

    // Notification channels
    bool     mqtt_enabled;           /**< Publish alerts to MQTT */
    bool     websocket_enabled;      /**< Publish alerts to WebSocket */

} alert_config_t;

/**
 * @brief Individual alert entry
 */
typedef struct {
    uint32_t         alert_id;       /**< Unique alert identifier (monotonic counter) */
    uint64_t         timestamp_ms;   /**< Alert trigger timestamp (ms since boot) */
    alert_type_t     type;           /**< Alert type */
    alert_severity_t severity;       /**< Severity level */
    alert_status_t   status;         /**< Current status */
    float            trigger_value;  /**< Value that triggered alert */
    float            threshold_value; /**< Configured threshold */
    char             message[ALERT_MANAGER_MESSAGE_MAX_LENGTH]; /**< Human-readable message */
} alert_entry_t;

/**
 * @brief Alert statistics
 */
typedef struct {
    uint32_t total_alerts_triggered;  /**< Total alerts since boot */
    uint32_t active_alert_count;      /**< Currently active alerts */
    uint32_t critical_count;          /**< Active critical alerts */
    uint32_t warning_count;           /**< Active warnings */
    uint32_t info_count;              /**< Active info alerts */
    uint32_t total_acknowledged;      /**< Total alerts acknowledged */
} alert_statistics_t;

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Initialize the alert manager module
 *
 * - Loads configuration from NVS
 * - Subscribes to event bus for UART_BMS live data events
 * - Initializes alert history circular buffer
 * - Starts alert monitoring task
 */
void alert_manager_init(void);

/**
 * @brief Set the event bus publisher for alert notifications
 * @param publisher Event bus publish function
 */
void alert_manager_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Get current alert configuration
 * @param out_config Pointer to store configuration
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_config(alert_config_t *out_config);

/**
 * @brief Update alert configuration and persist to NVS
 * @param config New configuration
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL
 */
esp_err_t alert_manager_set_config(const alert_config_t *config);

/**
 * @brief Get list of currently active alerts
 * @param out_alerts Array to store alerts
 * @param max_count  Maximum alerts to retrieve
 * @param out_count  Actual number of alerts returned
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_active_alerts(alert_entry_t *out_alerts,
                                           size_t max_count,
                                           size_t *out_count);

/**
 * @brief Get alert history (last N alerts)
 * @param out_alerts Array to store alerts
 * @param max_count  Maximum alerts to retrieve
 * @param out_count  Actual number of alerts returned
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_history(alert_entry_t *out_alerts,
                                     size_t max_count,
                                     size_t *out_count);

/**
 * @brief Acknowledge a specific alert by ID
 * @param alert_id Alert identifier to acknowledge
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if alert doesn't exist
 */
esp_err_t alert_manager_acknowledge(uint32_t alert_id);

/**
 * @brief Acknowledge all active alerts
 * @return ESP_OK on success
 */
esp_err_t alert_manager_acknowledge_all(void);

/**
 * @brief Get alert statistics
 * @param out_stats Pointer to store statistics
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_statistics(alert_statistics_t *out_stats);

/**
 * @brief Clear alert history (keeps active alerts)
 * @return ESP_OK on success
 */
esp_err_t alert_manager_clear_history(void);

/**
 * @brief Get configuration as JSON string
 * @param buffer      Output buffer
 * @param buffer_size Buffer size
 * @param out_length  Actual JSON length written
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length);

/**
 * @brief Set configuration from JSON string
 * @param json   JSON configuration string
 * @param length JSON string length
 * @return ESP_OK on success
 */
esp_err_t alert_manager_set_config_json(const char *json, size_t length);

/**
 * @brief Get active alerts as JSON string
 * @param buffer      Output buffer
 * @param buffer_size Buffer size
 * @param out_length  Actual JSON length written
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_active_alerts_json(char *buffer, size_t buffer_size, size_t *out_length);

/**
 * @brief Get alert history as JSON string
 * @param buffer      Output buffer
 * @param buffer_size Buffer size
 * @param out_length  Actual JSON length written
 * @param limit       Maximum number of history entries (0 = all)
 * @return ESP_OK on success
 */
esp_err_t alert_manager_get_history_json(char *buffer, size_t buffer_size, size_t *out_length, size_t limit);

#ifdef __cplusplus
}
#endif
