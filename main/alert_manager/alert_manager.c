/**
 * @file alert_manager.c
 * @brief Alert and notification management implementation
 */

#include "alert_manager.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#endif
#include "cJSON.h"

#include "uart_bms.h"

static const char *TAG = "alert_manager";

// =============================================================================
// Private structures and state
// =============================================================================

/**
 * @brief Alert manager state
 */
typedef struct {
    alert_config_t config;                /**< Current configuration */
    alert_entry_t  active[ALERT_MANAGER_MAX_ACTIVE_ALERTS]; /**< Active alerts */
    size_t         active_count;          /**< Number of active alerts */
    alert_entry_t  history[ALERT_MANAGER_MAX_HISTORY]; /**< Alert history circular buffer */
    size_t         history_head;          /**< History buffer write index */
    size_t         history_count;         /**< Number of entries in history */
    uint32_t       next_alert_id;         /**< Monotonic alert ID counter */
    alert_statistics_t stats;             /**< Runtime statistics */
    uint64_t       last_trigger_time_ms[256]; /**< Debounce timestamps per alert type */
    uint16_t       last_online_status;    /**< Previous TinyBMS online status for change detection */
    event_bus_publish_fn_t event_publisher; /**< Event bus publisher */
    SemaphoreHandle_t mutex;              /**< Thread safety mutex */
    bool           initialized;           /**< Initialization flag */
} alert_manager_state_t;

static alert_manager_state_t s_state = {0};

// Event bus subscription
static event_bus_subscription_handle_t s_uart_bms_subscription = NULL;

// NVS configuration keys
#define NVS_NAMESPACE     "alert_mgr"
#define NVS_KEY_CONFIG    "config"

// Event IDs
#define EVENT_ID_ALERT_TRIGGERED  0x20000001
#define EVENT_ID_ALERT_CLEARED    0x20000002
#define EVENT_ID_ALERT_ACKNOWLEDGED 0x20000003

// =============================================================================
// Forward declarations
// =============================================================================

static void alert_manager_check_thresholds(const uart_bms_live_data_t *data);
static void alert_manager_check_status_change(const uart_bms_live_data_t *data);
static void alert_manager_event_callback(const event_bus_event_t *event, void *context);
static void alert_manager_trigger_alert(alert_type_t type,
                                          alert_severity_t severity,
                                          float trigger_value,
                                          float threshold_value,
                                          const char *message);
static void alert_manager_add_to_history(const alert_entry_t *alert);
static void alert_manager_publish_alert_event(const alert_entry_t *alert, event_bus_event_id_t event_id);
static esp_err_t alert_manager_load_config(void);
static esp_err_t alert_manager_save_config(void);
static void alert_manager_init_default_config(alert_config_t *config);

// =============================================================================
// Default configuration
// =============================================================================

/**
 * @brief Initialize configuration with safe default values
 */
static void alert_manager_init_default_config(alert_config_t *config)
{
    memset(config, 0, sizeof(alert_config_t));

    config->enabled = true;
    config->debounce_sec = ALERT_MANAGER_DEFAULT_DEBOUNCE_SEC;

    // Temperature defaults (conservative for LiFePO4)
    config->temp_high_enabled = true;
    config->temperature_max_c = 50.0f;  // 50°C max
    config->temp_low_enabled = true;
    config->temperature_min_c = 0.0f;   // 0°C min for charging

    // Voltage defaults (LiFePO4 typical)
    config->cell_volt_high_enabled = true;
    config->cell_voltage_max_mv = 3650;  // 3.65V max per cell
    config->cell_volt_low_enabled = true;
    config->cell_voltage_min_mv = 2500;  // 2.5V min per cell

    config->pack_volt_high_enabled = false;  // Disabled by default
    config->pack_voltage_max_v = 58.4f;      // 16S * 3.65V
    config->pack_volt_low_enabled = false;
    config->pack_voltage_min_v = 40.0f;      // 16S * 2.5V

    // Current defaults
    config->current_discharge_enabled = true;
    config->discharge_current_max_a = 100.0f; // 100A max discharge
    config->current_charge_enabled = true;
    config->charge_current_max_a = 50.0f;     // 50A max charge

    // SOC defaults
    config->soc_low_enabled = true;
    config->soc_min_pct = 10.0f;  // Warning at 10% SOC
    config->soc_high_enabled = false;
    config->soc_max_pct = 95.0f;

    // Cell imbalance
    config->imbalance_enabled = true;
    config->cell_imbalance_max_mv = 100;  // 100mV max spread

    // TinyBMS monitoring
    config->monitor_tinybms_events = true;
    config->monitor_status_changes = true;

    // Notification channels
    config->mqtt_enabled = true;
    config->websocket_enabled = true;
}

// =============================================================================
// NVS persistence
// =============================================================================

#ifdef ESP_PLATFORM
/**
 * @brief Load configuration from NVS
 */
static esp_err_t alert_manager_load_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        alert_manager_init_default_config(&s_state.config);
        return ESP_OK;
    }

    size_t required_size = sizeof(alert_config_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, &s_state.config, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s, using defaults", esp_err_to_name(err));
        alert_manager_init_default_config(&s_state.config);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

/**
 * @brief Save configuration to NVS
 */
static esp_err_t alert_manager_save_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &s_state.config, sizeof(alert_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Configuration saved to NVS");
    return ESP_OK;
}
#else
/**
 * @brief Stub pour load config (builds de test/simulation)
 */
static esp_err_t alert_manager_load_config(void)
{
    ESP_LOGW(TAG, "NVS not available in simulation mode, using defaults");
    alert_manager_init_default_config(&s_state.config);
    return ESP_OK;
}

/**
 * @brief Stub pour save config (builds de test/simulation)
 */
static esp_err_t alert_manager_save_config(void)
{
    ESP_LOGW(TAG, "NVS not available in simulation mode, config not persisted");
    return ESP_OK;
}
#endif

// =============================================================================
// Alert lifecycle management
// =============================================================================

/**
 * @brief Trigger a new alert
 */
static void alert_manager_trigger_alert(alert_type_t type,
                                          alert_severity_t severity,
                                          float trigger_value,
                                          float threshold_value,
                                          const char *message)
{
    if (!s_state.config.enabled) {
        return;
    }

    // Check debounce
    uint64_t now_ms = (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint8_t type_index = (uint8_t)(type & 0xFF);
    if ((now_ms - s_state.last_trigger_time_ms[type_index]) < (s_state.config.debounce_sec * 1000)) {
        return; // Still in debounce period
    }
    s_state.last_trigger_time_ms[type_index] = now_ms;

    // Check if alert already active
    for (size_t i = 0; i < s_state.active_count; i++) {
        if (s_state.active[i].type == type) {
            return; // Already active, don't duplicate
        }
    }

    // Check capacity
    if (s_state.active_count >= ALERT_MANAGER_MAX_ACTIVE_ALERTS) {
        ESP_LOGW(TAG, "Active alerts buffer full, dropping new alert");
        return;
    }

    // Create new alert
    alert_entry_t *alert = &s_state.active[s_state.active_count++];
    alert->alert_id = s_state.next_alert_id++;
    alert->timestamp_ms = now_ms;
    alert->type = type;
    alert->severity = severity;
    alert->status = ALERT_STATUS_ACTIVE;
    alert->trigger_value = trigger_value;
    alert->threshold_value = threshold_value;
    snprintf(alert->message, ALERT_MANAGER_MESSAGE_MAX_LENGTH, "%s", message);

    // Update statistics
    s_state.stats.total_alerts_triggered++;
    s_state.stats.active_alert_count = s_state.active_count;
    switch (severity) {
        case ALERT_SEVERITY_CRITICAL:
            s_state.stats.critical_count++;
            break;
        case ALERT_SEVERITY_WARNING:
            s_state.stats.warning_count++;
            break;
        case ALERT_SEVERITY_INFO:
            s_state.stats.info_count++;
            break;
    }

    // Add to history
    alert_manager_add_to_history(alert);

    // Publish event
    alert_manager_publish_alert_event(alert, EVENT_ID_ALERT_TRIGGERED);

    ESP_LOGI(TAG, "Alert triggered: [%s] %s (ID:%lu)",
             severity == ALERT_SEVERITY_CRITICAL ? "CRITICAL" :
             severity == ALERT_SEVERITY_WARNING ? "WARNING" : "INFO",
             message, alert->alert_id);
}

/**
 * @brief Add alert to history circular buffer
 */
static void alert_manager_add_to_history(const alert_entry_t *alert)
{
    memcpy(&s_state.history[s_state.history_head], alert, sizeof(alert_entry_t));
    s_state.history_head = (s_state.history_head + 1) % ALERT_MANAGER_MAX_HISTORY;
    if (s_state.history_count < ALERT_MANAGER_MAX_HISTORY) {
        s_state.history_count++;
    }
}

/**
 * @brief Publish alert event to event bus
 */
static void alert_manager_publish_alert_event(const alert_entry_t *alert, event_bus_event_id_t event_id)
{
    if (s_state.event_publisher == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = event_id,
        .payload = alert,
        .payload_size = sizeof(alert_entry_t)
    };

    s_state.event_publisher(&event, 0);
}

// =============================================================================
// Threshold checking
// =============================================================================

/**
 * @brief Check all configured thresholds against live data
 */
static void alert_manager_check_thresholds(const uart_bms_live_data_t *data)
{
    char msg[ALERT_MANAGER_MESSAGE_MAX_LENGTH];

    // Temperature checks
    if (s_state.config.temp_high_enabled) {
        if (data->average_temperature_c > s_state.config.temperature_max_c) {
            snprintf(msg, sizeof(msg), "Temperature too high: %.1f°C (max: %.1f°C)",
                     data->average_temperature_c, s_state.config.temperature_max_c);
            alert_manager_trigger_alert(ALERT_TYPE_TEMPERATURE_HIGH, ALERT_SEVERITY_WARNING,
                                         data->average_temperature_c, s_state.config.temperature_max_c, msg);
        }
    }

    if (s_state.config.temp_low_enabled) {
        if (data->average_temperature_c < s_state.config.temperature_min_c) {
            snprintf(msg, sizeof(msg), "Temperature too low: %.1f°C (min: %.1f°C)",
                     data->average_temperature_c, s_state.config.temperature_min_c);
            alert_manager_trigger_alert(ALERT_TYPE_TEMPERATURE_LOW, ALERT_SEVERITY_WARNING,
                                         data->average_temperature_c, s_state.config.temperature_min_c, msg);
        }
    }

    // Cell voltage checks
    if (s_state.config.cell_volt_high_enabled) {
        if (data->max_cell_mv > s_state.config.cell_voltage_max_mv) {
            snprintf(msg, sizeof(msg), "Cell voltage too high: %u mV (max: %u mV)",
                     data->max_cell_mv, s_state.config.cell_voltage_max_mv);
            alert_manager_trigger_alert(ALERT_TYPE_CELL_VOLTAGE_HIGH, ALERT_SEVERITY_CRITICAL,
                                         data->max_cell_mv, s_state.config.cell_voltage_max_mv, msg);
        }
    }

    if (s_state.config.cell_volt_low_enabled) {
        if (data->min_cell_mv < s_state.config.cell_voltage_min_mv) {
            snprintf(msg, sizeof(msg), "Cell voltage too low: %u mV (min: %u mV)",
                     data->min_cell_mv, s_state.config.cell_voltage_min_mv);
            alert_manager_trigger_alert(ALERT_TYPE_CELL_VOLTAGE_LOW, ALERT_SEVERITY_CRITICAL,
                                         data->min_cell_mv, s_state.config.cell_voltage_min_mv, msg);
        }
    }

    // Pack voltage checks
    if (s_state.config.pack_volt_high_enabled) {
        if (data->pack_voltage_v > s_state.config.pack_voltage_max_v) {
            snprintf(msg, sizeof(msg), "Pack voltage too high: %.2f V (max: %.2f V)",
                     data->pack_voltage_v, s_state.config.pack_voltage_max_v);
            alert_manager_trigger_alert(ALERT_TYPE_PACK_VOLTAGE_HIGH, ALERT_SEVERITY_WARNING,
                                         data->pack_voltage_v, s_state.config.pack_voltage_max_v, msg);
        }
    }

    if (s_state.config.pack_volt_low_enabled) {
        if (data->pack_voltage_v < s_state.config.pack_voltage_min_v) {
            snprintf(msg, sizeof(msg), "Pack voltage too low: %.2f V (min: %.2f V)",
                     data->pack_voltage_v, s_state.config.pack_voltage_min_v);
            alert_manager_trigger_alert(ALERT_TYPE_PACK_VOLTAGE_LOW, ALERT_SEVERITY_WARNING,
                                         data->pack_voltage_v, s_state.config.pack_voltage_min_v, msg);
        }
    }

    // Current checks (discharge = negative current)
    if (s_state.config.current_discharge_enabled) {
        float discharge_current = fabsf(data->pack_current_a < 0.0f ? data->pack_current_a : 0.0f);
        if (discharge_current > s_state.config.discharge_current_max_a) {
            snprintf(msg, sizeof(msg), "Discharge current too high: %.1f A (max: %.1f A)",
                     discharge_current, s_state.config.discharge_current_max_a);
            alert_manager_trigger_alert(ALERT_TYPE_CURRENT_DISCHARGE_HIGH, ALERT_SEVERITY_WARNING,
                                         discharge_current, s_state.config.discharge_current_max_a, msg);
        }
    }

    if (s_state.config.current_charge_enabled) {
        float charge_current = data->pack_current_a > 0.0f ? data->pack_current_a : 0.0f;
        if (charge_current > s_state.config.charge_current_max_a) {
            snprintf(msg, sizeof(msg), "Charge current too high: %.1f A (max: %.1f A)",
                     charge_current, s_state.config.charge_current_max_a);
            alert_manager_trigger_alert(ALERT_TYPE_CURRENT_CHARGE_HIGH, ALERT_SEVERITY_WARNING,
                                         charge_current, s_state.config.charge_current_max_a, msg);
        }
    }

    // SOC checks
    if (s_state.config.soc_low_enabled) {
        if (data->state_of_charge_pct < s_state.config.soc_min_pct) {
            snprintf(msg, sizeof(msg), "SOC too low: %.1f%% (min: %.1f%%)",
                     data->state_of_charge_pct, s_state.config.soc_min_pct);
            alert_manager_trigger_alert(ALERT_TYPE_SOC_LOW, ALERT_SEVERITY_WARNING,
                                         data->state_of_charge_pct, s_state.config.soc_min_pct, msg);
        }
    }

    if (s_state.config.soc_high_enabled) {
        if (data->state_of_charge_pct > s_state.config.soc_max_pct) {
            snprintf(msg, sizeof(msg), "SOC too high: %.1f%% (max: %.1f%%)",
                     data->state_of_charge_pct, s_state.config.soc_max_pct);
            alert_manager_trigger_alert(ALERT_TYPE_SOC_HIGH, ALERT_SEVERITY_INFO,
                                         data->state_of_charge_pct, s_state.config.soc_max_pct, msg);
        }
    }

    // Cell imbalance check
    if (s_state.config.imbalance_enabled) {
        uint16_t imbalance = data->max_cell_mv - data->min_cell_mv;
        if (imbalance > s_state.config.cell_imbalance_max_mv) {
            snprintf(msg, sizeof(msg), "Cell imbalance too high: %u mV (max: %u mV)",
                     imbalance, s_state.config.cell_imbalance_max_mv);
            alert_manager_trigger_alert(ALERT_TYPE_CELL_IMBALANCE_HIGH, ALERT_SEVERITY_WARNING,
                                         imbalance, s_state.config.cell_imbalance_max_mv, msg);
        }
    }
}

/**
 * @brief Check for TinyBMS online status changes
 *
 * Detects and reports changes in TinyBMS operational status (Reg:50).
 * Status values from documentation:
 * - 0x91: Charging
 * - 0x92: Fully Charged
 * - 0x93: Discharging
 * - 0x96: Regeneration
 * - 0x97: Idle
 * - 0x9B: Fault
 */
static void alert_manager_check_status_change(const uart_bms_live_data_t *data)
{
    if (!s_state.config.monitor_status_changes) {
        return;
    }

    // Find online status register in the monitored registers
    uint16_t current_status = 0;
    bool status_found = false;

    for (size_t i = 0; i < data->register_count; i++) {
        if (data->registers[i].address == 50) {  // Reg:50 = Online Status
            current_status = data->registers[i].raw_value;
            status_found = true;
            break;
        }
    }

    if (!status_found) {
        return; // Status not available in this update
    }

    // Check if status changed
    if (current_status == s_state.last_online_status) {
        return; // No change
    }

    // Status changed, trigger appropriate alert
    char msg[ALERT_MANAGER_MESSAGE_MAX_LENGTH];
    alert_type_t alert_type;
    alert_severity_t severity;
    const char *status_name = "Unknown";

    switch (current_status) {
        case 0x91:
            alert_type = ALERT_TYPE_STATUS_CHARGING;
            severity = ALERT_SEVERITY_INFO;
            status_name = "Charging";
            break;
        case 0x92:
            alert_type = ALERT_TYPE_STATUS_FULLY_CHARGED;
            severity = ALERT_SEVERITY_INFO;
            status_name = "Fully Charged";
            break;
        case 0x93:
            alert_type = ALERT_TYPE_STATUS_DISCHARGING;
            severity = ALERT_SEVERITY_INFO;
            status_name = "Discharging";
            break;
        case 0x96:
            alert_type = ALERT_TYPE_STATUS_REGENERATION;
            severity = ALERT_SEVERITY_INFO;
            status_name = "Regeneration";
            break;
        case 0x97:
            alert_type = ALERT_TYPE_STATUS_IDLE;
            severity = ALERT_SEVERITY_INFO;
            status_name = "Idle";
            break;
        case 0x9B:
            alert_type = ALERT_TYPE_STATUS_FAULT;
            severity = ALERT_SEVERITY_CRITICAL;
            status_name = "Fault";
            break;
        default:
            ESP_LOGW(TAG, "Unknown TinyBMS status: 0x%02X", current_status);
            s_state.last_online_status = current_status;
            return;
    }

    snprintf(msg, sizeof(msg), "TinyBMS status changed to: %s (0x%02X)", status_name, current_status);
    alert_manager_trigger_alert(alert_type, severity, current_status, 0, msg);

    s_state.last_online_status = current_status;
}

// =============================================================================
// Event bus callback
// =============================================================================

/**
 * @brief Event bus callback for UART BMS live data
 */
static void alert_manager_event_callback(const event_bus_event_t *event, void *context)
{
    (void)context;

    // We're interested in UART BMS live data events
    // Event ID for UART BMS data is defined in the system (typically 0x10000001)
    if (event->payload == NULL || event->payload_size != sizeof(uart_bms_live_data_t)) {
        return;
    }

    const uart_bms_live_data_t *data = (const uart_bms_live_data_t *)event->payload;

    // Acquire mutex for thread safety
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for alert checking");
        return;
    }

    // Perform all checks
    alert_manager_check_thresholds(data);
    alert_manager_check_status_change(data);

    xSemaphoreGive(s_state.mutex);
}

// =============================================================================
// Public API implementation
// =============================================================================

/**
 * @brief Initialize alert manager
 */
void alert_manager_init(void)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Alert manager already initialized");
        return;
    }

    memset(&s_state, 0, sizeof(s_state));

    // Create mutex
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Load configuration from NVS
    alert_manager_load_config();

    // Subscribe to event bus for UART BMS data
    event_bus_init();
    s_uart_bms_subscription = event_bus_subscribe_default(alert_manager_event_callback, NULL);
    if (s_uart_bms_subscription == NULL) {
        ESP_LOGE(TAG, "Failed to subscribe to event bus");
        return;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "Alert manager initialized successfully");
}

/**
 * @brief Set event publisher
 */
void alert_manager_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_state.event_publisher = publisher;
}

/**
 * @brief Get current configuration
 */
esp_err_t alert_manager_get_config(alert_config_t *out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(out_config, &s_state.config, sizeof(alert_config_t));

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

/**
 * @brief Set configuration
 */
esp_err_t alert_manager_set_config(const alert_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(&s_state.config, config, sizeof(alert_config_t));
    esp_err_t err = alert_manager_save_config();

    xSemaphoreGive(s_state.mutex);

    ESP_LOGI(TAG, "Configuration updated");
    return err;
}

/**
 * @brief Get active alerts
 */
esp_err_t alert_manager_get_active_alerts(alert_entry_t *out_alerts,
                                           size_t max_count,
                                           size_t *out_count)
{
    if (out_alerts == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t count = s_state.active_count < max_count ? s_state.active_count : max_count;
    memcpy(out_alerts, s_state.active, count * sizeof(alert_entry_t));
    *out_count = count;

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

/**
 * @brief Get alert history
 */
esp_err_t alert_manager_get_history(alert_entry_t *out_alerts,
                                     size_t max_count,
                                     size_t *out_count)
{
    if (out_alerts == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t count = s_state.history_count < max_count ? s_state.history_count : max_count;

    // Copy from circular buffer (most recent first)
    size_t copy_index = 0;
    for (size_t i = 0; i < count; i++) {
        size_t history_index = (s_state.history_head + ALERT_MANAGER_MAX_HISTORY - i - 1) % ALERT_MANAGER_MAX_HISTORY;
        memcpy(&out_alerts[copy_index++], &s_state.history[history_index], sizeof(alert_entry_t));
    }

    *out_count = count;

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

/**
 * @brief Acknowledge alert by ID
 */
esp_err_t alert_manager_acknowledge(uint32_t alert_id)
{
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;

    for (size_t i = 0; i < s_state.active_count; i++) {
        if (s_state.active[i].alert_id == alert_id) {
            s_state.active[i].status = ALERT_STATUS_ACKNOWLEDGED;
            s_state.stats.total_acknowledged++;

            // Publish acknowledgement event
            alert_manager_publish_alert_event(&s_state.active[i], EVENT_ID_ALERT_ACKNOWLEDGED);

            ESP_LOGI(TAG, "Alert %lu acknowledged", alert_id);
            result = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(s_state.mutex);
    return result;
}

/**
 * @brief Acknowledge all alerts
 */
esp_err_t alert_manager_acknowledge_all(void)
{
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < s_state.active_count; i++) {
        if (s_state.active[i].status == ALERT_STATUS_ACTIVE) {
            s_state.active[i].status = ALERT_STATUS_ACKNOWLEDGED;
            s_state.stats.total_acknowledged++;
            alert_manager_publish_alert_event(&s_state.active[i], EVENT_ID_ALERT_ACKNOWLEDGED);
        }
    }

    ESP_LOGI(TAG, "All alerts acknowledged (%zu)", s_state.active_count);

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

/**
 * @brief Get statistics
 */
esp_err_t alert_manager_get_statistics(alert_statistics_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(out_stats, &s_state.stats, sizeof(alert_statistics_t));

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

/**
 * @brief Clear history
 */
esp_err_t alert_manager_clear_history(void)
{
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_state.history_head = 0;
    s_state.history_count = 0;
    memset(s_state.history, 0, sizeof(s_state.history));

    ESP_LOGI(TAG, "Alert history cleared");

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

// =============================================================================
// JSON serialization (implementation continues in next part)
// =============================================================================

/**
 * @brief Get configuration as JSON
 */
esp_err_t alert_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || out_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }

    // Serialize configuration
    cJSON_AddBoolToObject(root, "enabled", s_state.config.enabled);
    cJSON_AddNumberToObject(root, "debounce_sec", s_state.config.debounce_sec);

    cJSON *temp = cJSON_AddObjectToObject(root, "temperature");
    cJSON_AddBoolToObject(temp, "high_enabled", s_state.config.temp_high_enabled);
    cJSON_AddNumberToObject(temp, "max_c", s_state.config.temperature_max_c);
    cJSON_AddBoolToObject(temp, "low_enabled", s_state.config.temp_low_enabled);
    cJSON_AddNumberToObject(temp, "min_c", s_state.config.temperature_min_c);

    cJSON *cell_volt = cJSON_AddObjectToObject(root, "cell_voltage");
    cJSON_AddBoolToObject(cell_volt, "high_enabled", s_state.config.cell_volt_high_enabled);
    cJSON_AddNumberToObject(cell_volt, "max_mv", s_state.config.cell_voltage_max_mv);
    cJSON_AddBoolToObject(cell_volt, "low_enabled", s_state.config.cell_volt_low_enabled);
    cJSON_AddNumberToObject(cell_volt, "min_mv", s_state.config.cell_voltage_min_mv);

    cJSON *pack_volt = cJSON_AddObjectToObject(root, "pack_voltage");
    cJSON_AddBoolToObject(pack_volt, "high_enabled", s_state.config.pack_volt_high_enabled);
    cJSON_AddNumberToObject(pack_volt, "max_v", s_state.config.pack_voltage_max_v);
    cJSON_AddBoolToObject(pack_volt, "low_enabled", s_state.config.pack_volt_low_enabled);
    cJSON_AddNumberToObject(pack_volt, "min_v", s_state.config.pack_voltage_min_v);

    cJSON *current = cJSON_AddObjectToObject(root, "current");
    cJSON_AddBoolToObject(current, "discharge_enabled", s_state.config.current_discharge_enabled);
    cJSON_AddNumberToObject(current, "discharge_max_a", s_state.config.discharge_current_max_a);
    cJSON_AddBoolToObject(current, "charge_enabled", s_state.config.current_charge_enabled);
    cJSON_AddNumberToObject(current, "charge_max_a", s_state.config.charge_current_max_a);

    cJSON *soc = cJSON_AddObjectToObject(root, "soc");
    cJSON_AddBoolToObject(soc, "low_enabled", s_state.config.soc_low_enabled);
    cJSON_AddNumberToObject(soc, "min_pct", s_state.config.soc_min_pct);
    cJSON_AddBoolToObject(soc, "high_enabled", s_state.config.soc_high_enabled);
    cJSON_AddNumberToObject(soc, "max_pct", s_state.config.soc_max_pct);

    cJSON *imbalance = cJSON_AddObjectToObject(root, "imbalance");
    cJSON_AddBoolToObject(imbalance, "enabled", s_state.config.imbalance_enabled);
    cJSON_AddNumberToObject(imbalance, "max_mv", s_state.config.cell_imbalance_max_mv);

    cJSON_AddBoolToObject(root, "monitor_tinybms_events", s_state.config.monitor_tinybms_events);
    cJSON_AddBoolToObject(root, "monitor_status_changes", s_state.config.monitor_status_changes);
    cJSON_AddBoolToObject(root, "mqtt_enabled", s_state.config.mqtt_enabled);
    cJSON_AddBoolToObject(root, "websocket_enabled", s_state.config.websocket_enabled);

    xSemaphoreGive(s_state.mutex);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(json_str);
    if (len >= buffer_size) {
        free(json_str);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(buffer, json_str);
    *out_length = len;
    free(json_str);

    return ESP_OK;
}

/**
 * @brief Set configuration from JSON
 */
esp_err_t alert_manager_set_config_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Parse and update configuration
    alert_config_t new_config = s_state.config;  // Start with current config

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "enabled")) != NULL && cJSON_IsBool(item)) {
        new_config.enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "debounce_sec")) != NULL && cJSON_IsNumber(item)) {
        new_config.debounce_sec = (uint32_t)item->valueint;
    }

    // Temperature
    cJSON *temp = cJSON_GetObjectItem(root, "temperature");
    if (temp != NULL) {
        if ((item = cJSON_GetObjectItem(temp, "high_enabled")) != NULL) new_config.temp_high_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(temp, "max_c")) != NULL) new_config.temperature_max_c = (float)item->valuedouble;
        if ((item = cJSON_GetObjectItem(temp, "low_enabled")) != NULL) new_config.temp_low_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(temp, "min_c")) != NULL) new_config.temperature_min_c = (float)item->valuedouble;
    }

    // Cell voltage
    cJSON *cell_volt = cJSON_GetObjectItem(root, "cell_voltage");
    if (cell_volt != NULL) {
        if ((item = cJSON_GetObjectItem(cell_volt, "high_enabled")) != NULL) new_config.cell_volt_high_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(cell_volt, "max_mv")) != NULL) new_config.cell_voltage_max_mv = (uint16_t)item->valueint;
        if ((item = cJSON_GetObjectItem(cell_volt, "low_enabled")) != NULL) new_config.cell_volt_low_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(cell_volt, "min_mv")) != NULL) new_config.cell_voltage_min_mv = (uint16_t)item->valueint;
    }

    // Pack voltage
    cJSON *pack_volt = cJSON_GetObjectItem(root, "pack_voltage");
    if (pack_volt != NULL) {
        if ((item = cJSON_GetObjectItem(pack_volt, "high_enabled")) != NULL) new_config.pack_volt_high_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(pack_volt, "max_v")) != NULL) new_config.pack_voltage_max_v = (float)item->valuedouble;
        if ((item = cJSON_GetObjectItem(pack_volt, "low_enabled")) != NULL) new_config.pack_volt_low_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(pack_volt, "min_v")) != NULL) new_config.pack_voltage_min_v = (float)item->valuedouble;
    }

    // Current
    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current != NULL) {
        if ((item = cJSON_GetObjectItem(current, "discharge_enabled")) != NULL) new_config.current_discharge_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(current, "discharge_max_a")) != NULL) new_config.discharge_current_max_a = (float)item->valuedouble;
        if ((item = cJSON_GetObjectItem(current, "charge_enabled")) != NULL) new_config.current_charge_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(current, "charge_max_a")) != NULL) new_config.charge_current_max_a = (float)item->valuedouble;
    }

    // SOC
    cJSON *soc = cJSON_GetObjectItem(root, "soc");
    if (soc != NULL) {
        if ((item = cJSON_GetObjectItem(soc, "low_enabled")) != NULL) new_config.soc_low_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(soc, "min_pct")) != NULL) new_config.soc_min_pct = (float)item->valuedouble;
        if ((item = cJSON_GetObjectItem(soc, "high_enabled")) != NULL) new_config.soc_high_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(soc, "max_pct")) != NULL) new_config.soc_max_pct = (float)item->valuedouble;
    }

    // Imbalance
    cJSON *imbalance = cJSON_GetObjectItem(root, "imbalance");
    if (imbalance != NULL) {
        if ((item = cJSON_GetObjectItem(imbalance, "enabled")) != NULL) new_config.imbalance_enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(imbalance, "max_mv")) != NULL) new_config.cell_imbalance_max_mv = (uint16_t)item->valueint;
    }

    // Monitoring flags
    if ((item = cJSON_GetObjectItem(root, "monitor_tinybms_events")) != NULL) new_config.monitor_tinybms_events = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "monitor_status_changes")) != NULL) new_config.monitor_status_changes = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "mqtt_enabled")) != NULL) new_config.mqtt_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "websocket_enabled")) != NULL) new_config.websocket_enabled = cJSON_IsTrue(item);

    cJSON_Delete(root);

    // Apply new configuration
    return alert_manager_set_config(&new_config);
}

/**
 * @brief Get active alerts as JSON
 */
esp_err_t alert_manager_get_active_alerts_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || out_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    alert_entry_t alerts[ALERT_MANAGER_MAX_ACTIVE_ALERTS];
    size_t count = 0;

    esp_err_t err = alert_manager_get_active_alerts(alerts, ALERT_MANAGER_MAX_ACTIVE_ALERTS, &count);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < count; i++) {
        cJSON *alert_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(alert_obj, "id", alerts[i].alert_id);
        cJSON_AddNumberToObject(alert_obj, "timestamp_ms", (double)alerts[i].timestamp_ms);
        cJSON_AddNumberToObject(alert_obj, "type", alerts[i].type);
        cJSON_AddNumberToObject(alert_obj, "severity", alerts[i].severity);
        cJSON_AddNumberToObject(alert_obj, "status", alerts[i].status);
        cJSON_AddNumberToObject(alert_obj, "trigger_value", alerts[i].trigger_value);
        cJSON_AddNumberToObject(alert_obj, "threshold_value", alerts[i].threshold_value);
        cJSON_AddStringToObject(alert_obj, "message", alerts[i].message);
        cJSON_AddItemToArray(root, alert_obj);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(json_str);
    if (len >= buffer_size) {
        free(json_str);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(buffer, json_str);
    *out_length = len;
    free(json_str);

    return ESP_OK;
}

/**
 * @brief Get alert history as JSON
 */
esp_err_t alert_manager_get_history_json(char *buffer, size_t buffer_size, size_t *out_length, size_t limit)
{
    if (buffer == NULL || out_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_alerts = (limit == 0) ? ALERT_MANAGER_MAX_HISTORY : limit;
    if (max_alerts > ALERT_MANAGER_MAX_HISTORY) {
        max_alerts = ALERT_MANAGER_MAX_HISTORY;
    }

    alert_entry_t *alerts = malloc(max_alerts * sizeof(alert_entry_t));
    if (alerts == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t count = 0;
    esp_err_t err = alert_manager_get_history(alerts, max_alerts, &count);
    if (err != ESP_OK) {
        free(alerts);
        return err;
    }

    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        free(alerts);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < count; i++) {
        cJSON *alert_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(alert_obj, "id", alerts[i].alert_id);
        cJSON_AddNumberToObject(alert_obj, "timestamp_ms", (double)alerts[i].timestamp_ms);
        cJSON_AddNumberToObject(alert_obj, "type", alerts[i].type);
        cJSON_AddNumberToObject(alert_obj, "severity", alerts[i].severity);
        cJSON_AddNumberToObject(alert_obj, "status", alerts[i].status);
        cJSON_AddNumberToObject(alert_obj, "trigger_value", alerts[i].trigger_value);
        cJSON_AddNumberToObject(alert_obj, "threshold_value", alerts[i].threshold_value);
        cJSON_AddStringToObject(alert_obj, "message", alerts[i].message);
        cJSON_AddItemToArray(root, alert_obj);
    }

    free(alerts);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(json_str);
    if (len >= buffer_size) {
        free(json_str);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(buffer, json_str);
    *out_length = len;
    free(json_str);

    return ESP_OK;
}
