#include "config_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "app_events.h"
#include "uart_bms.h"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#endif

#define CONFIG_MANAGER_REGISTER_EVENT_BUFFERS 4
#define CONFIG_MANAGER_MAX_UPDATE_PAYLOAD     192
#define CONFIG_MANAGER_MAX_REGISTER_KEY       32
#define CONFIG_MANAGER_NAMESPACE              "gateway_cfg"
#define CONFIG_MANAGER_POLL_KEY               "uart_poll"

typedef struct {
    const char *key;
    const char *label;
    const char *unit;
    float min_value;
    float max_value;
    float step;
    float default_value;
    uint16_t address;
} config_manager_register_descriptor_t;

static const config_manager_register_descriptor_t s_register_descriptors[] = {
    {"charge_voltage_limit", "Charge Voltage Limit", "V", 48.0f, 58.4f, 0.1f, 54.8f, 0x0200},
    {"discharge_voltage_limit", "Discharge Voltage Limit", "V", 40.0f, 53.0f, 0.1f, 46.0f, 0x0201},
    {"charge_current_limit", "Charge Current Limit", "A", 5.0f, 200.0f, 0.5f, 80.0f, 0x0202},
    {"discharge_current_limit", "Discharge Current Limit", "A", 5.0f, 300.0f, 0.5f, 120.0f, 0x0203},
    {"cell_balance_start", "Cell Balance Start", "mV", 3400.0f, 3700.0f, 10.0f, 3500.0f, 0x0204},
    {"cell_balance_stop", "Cell Balance Stop", "mV", 3450.0f, 3800.0f, 10.0f, 3550.0f, 0x0205},
    {"cell_overvoltage", "Cell Overvoltage", "mV", 3600.0f, 4000.0f, 10.0f, 3800.0f, 0x0206},
    {"cell_undervoltage", "Cell Undervoltage", "mV", 2500.0f, 3200.0f, 10.0f, 2800.0f, 0x0207},
    {"pack_overvoltage_release", "Pack Overvoltage Release", "V", 48.0f, 60.0f, 0.1f, 57.0f, 0x0208},
    {"pack_undervoltage_release", "Pack Undervoltage Release", "V", 40.0f, 55.0f, 0.1f, 44.0f, 0x0209},
    {"charge_temp_min", "Charge Temperature Min", "°C", -20.0f, 20.0f, 1.0f, 0.0f, 0x020A},
    {"charge_temp_max", "Charge Temperature Max", "°C", 20.0f, 60.0f, 1.0f, 45.0f, 0x020B},
    {"discharge_temp_min", "Discharge Temperature Min", "°C", -40.0f, 20.0f, 1.0f, -10.0f, 0x020C},
    {"discharge_temp_max", "Discharge Temperature Max", "°C", 20.0f, 80.0f, 1.0f, 55.0f, 0x020D},
    {"storage_voltage", "Storage Voltage", "V", 45.0f, 54.0f, 0.1f, 50.0f, 0x020E},
    {"heater_enable_temp", "Heater Enable Temperature", "°C", -20.0f, 20.0f, 1.0f, 5.0f, 0x020F},
    {"heater_disable_temp", "Heater Disable Temperature", "°C", 0.0f, 40.0f, 1.0f, 15.0f, 0x0210},
    {"soc_calibration_low", "SOC Calibration Low", "%", 0.0f, 50.0f, 0.5f, 10.0f, 0x0211},
    {"soc_calibration_high", "SOC Calibration High", "%", 50.0f, 100.0f, 0.5f, 90.0f, 0x0212},
    {"current_calibration", "Current Sensor Calibration", "%", 80.0f, 120.0f, 0.5f, 100.0f, 0x0213},
    {"voltage_calibration", "Voltage Calibration", "%", 95.0f, 105.0f, 0.1f, 100.0f, 0x0214},
    {"shunt_resistance", "Shunt Resistance", "mΩ", 0.1f, 5.0f, 0.1f, 1.5f, 0x0215},
    {"precharge_delay", "Precharge Delay", "ms", 0.0f, 5000.0f, 50.0f, 1500.0f, 0x0216},
    {"precharge_voltage", "Precharge Voltage", "V", 10.0f, 60.0f, 0.5f, 48.0f, 0x0217},
    {"balance_timeout", "Balance Timeout", "s", 0.0f, 600.0f, 10.0f, 180.0f, 0x0218},
    {"balance_trigger_delta", "Balance Trigger Delta", "mV", 5.0f, 50.0f, 1.0f, 15.0f, 0x0219},
    {"charge_current_cutoff", "Charge Current Cut-off", "A", 1.0f, 50.0f, 0.5f, 10.0f, 0x021A},
    {"float_voltage", "Float Voltage", "V", 48.0f, 56.0f, 0.1f, 54.0f, 0x021B},
    {"buzzer_enable", "Buzzer Enable", "bool", 0.0f, 1.0f, 1.0f, 1.0f, 0x021C},
    {"relay_hold_time", "Relay Hold Time", "ms", 0.0f, 5000.0f, 50.0f, 1000.0f, 0x021D},
    {"aux_output_mode", "Auxiliary Output Mode", "enum", 0.0f, 3.0f, 1.0f, 0.0f, 0x021E},
    {"can_node_id", "CAN Node ID", "", 1.0f, 253.0f, 1.0f, 42.0f, 0x021F},
    {"modbus_address", "Modbus Address", "", 1.0f, 247.0f, 1.0f, 1.0f, 0x0220},
    {"log_interval", "Log Interval", "s", 1.0f, 3600.0f, 1.0f, 60.0f, 0x0221},
    {"alarm_reset_delay", "Alarm Reset Delay", "s", 1.0f, 600.0f, 5.0f, 30.0f, 0x0222},
};

static const size_t s_register_count = sizeof(s_register_descriptors) / sizeof(s_register_descriptors[0]);

static const char *TAG = "config_manager";

static event_bus_publish_fn_t s_event_publisher = NULL;
static char s_config_json[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
static size_t s_config_length = 0;
static float s_register_values[sizeof(s_register_descriptors) / sizeof(s_register_descriptors[0])];
static bool s_registers_initialised = false;
static char s_register_events[CONFIG_MANAGER_REGISTER_EVENT_BUFFERS][CONFIG_MANAGER_MAX_UPDATE_PAYLOAD];
static size_t s_next_register_event = 0;
static uint32_t s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
static bool s_settings_loaded = false;
#ifdef ESP_PLATFORM
static bool s_nvs_initialised = false;
#endif

static bool config_manager_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
{
    if (buffer == NULL || buffer_size == 0 || offset == NULL) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *offset, buffer_size - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    size_t remaining = buffer_size - *offset;
    if ((size_t)written >= remaining) {
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static uint32_t config_manager_clamp_poll_interval(uint32_t interval_ms)
{
    if (interval_ms < UART_BMS_MIN_POLL_INTERVAL_MS) {
        return UART_BMS_MIN_POLL_INTERVAL_MS;
    }
    if (interval_ms > UART_BMS_MAX_POLL_INTERVAL_MS) {
        return UART_BMS_MAX_POLL_INTERVAL_MS;
    }
    return interval_ms;
}

#ifdef ESP_PLATFORM
static esp_err_t config_manager_init_nvs(void)
{
    if (s_nvs_initialised) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS partition due to %s", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            return erase_err;
        }
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_nvs_initialised = true;
    } else {
        ESP_LOGW(TAG, "Failed to initialise NVS: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

static void config_manager_load_persistent_settings(void)
{
    if (s_settings_loaded) {
        return;
    }

    s_settings_loaded = true;
#ifdef ESP_PLATFORM
    if (config_manager_init_nvs() != ESP_OK) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    uint32_t stored_interval = 0;
    err = nvs_get_u32(handle, CONFIG_MANAGER_POLL_KEY, &stored_interval);
    if (err == ESP_OK) {
        s_uart_poll_interval_ms = config_manager_clamp_poll_interval(stored_interval);
    }
    nvs_close(handle);
#else
    s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
#endif
}

static esp_err_t config_manager_store_poll_interval(uint32_t interval_ms)
{
#ifdef ESP_PLATFORM
    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, CONFIG_MANAGER_POLL_KEY, interval_ms);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
#else
    (void)interval_ms;
    return ESP_OK;
#endif
}

static void config_manager_publish_config_snapshot(void)
{
    if (s_event_publisher == NULL || s_config_length == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CONFIG_UPDATED,
        .payload = s_config_json,
        .payload_size = s_config_length + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish configuration snapshot");
    }
}

static void config_manager_publish_register_change(const char *key, float value)
{
    if (s_event_publisher == NULL || key == NULL) {
        return;
    }

    size_t slot = s_next_register_event;
    s_next_register_event = (s_next_register_event + 1) % CONFIG_MANAGER_REGISTER_EVENT_BUFFERS;

    char *payload = s_register_events[slot];
    int written = snprintf(payload,
                           CONFIG_MANAGER_MAX_UPDATE_PAYLOAD,
                           "{\"type\":\"register_update\",\"key\":\"%s\",\"value\":%.3f}",
                           key,
                           value);
    if (written < 0 || written >= CONFIG_MANAGER_MAX_UPDATE_PAYLOAD) {
        ESP_LOGW(TAG, "Register update payload truncated for %s", key);
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CONFIG_UPDATED,
        .payload = payload,
        .payload_size = (size_t)written + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish register update for %s", key);
    }
}

static esp_err_t config_manager_build_config_snapshot(void)
{
    int written = snprintf(s_config_json,
                           sizeof(s_config_json),
                           "{\"device\":\"TinyBMS Gateway\",\"version\":1,\"register_count\":%zu,"
                           "\"uart_poll_interval_ms\":%u,\"uart_poll_interval_min_ms\":%u,"
                           "\"uart_poll_interval_max_ms\":%u}",
                           s_register_count,
                           (unsigned)s_uart_poll_interval_ms,
                           (unsigned)UART_BMS_MIN_POLL_INTERVAL_MS,
                           (unsigned)UART_BMS_MAX_POLL_INTERVAL_MS);
    if (written < 0) {
        return ESP_FAIL;
    }

    if ((size_t)written >= sizeof(s_config_json)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_config_length = (size_t)written;
    return ESP_OK;
}

static void config_manager_load_register_defaults(void)
{
    for (size_t i = 0; i < s_register_count; ++i) {
        s_register_values[i] = s_register_descriptors[i].default_value;
    }
    s_registers_initialised = true;
}

static bool config_manager_find_register(const char *key, size_t *index_out)
{
    if (key == NULL) {
        return false;
    }

    for (size_t i = 0; i < s_register_count; ++i) {
        if (strcmp(s_register_descriptors[i].key, key) == 0) {
            if (index_out != NULL) {
                *index_out = i;
            }
            return true;
        }
    }

    return false;
}

static bool config_manager_extract_string_field(const char *json, const char *field, char *out, size_t out_size)
{
    if (json == NULL || field == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor != '"') {
        return false;
    }
    ++cursor;

    size_t len = 0;
    while (cursor[len] != '\0' && cursor[len] != '"') {
        if (len + 1 >= out_size) {
            return false;
        }
        out[len] = cursor[len];
        ++len;
    }

    if (cursor[len] != '"') {
        return false;
    }

    out[len] = '\0';
    return true;
}

static bool config_manager_extract_float_field(const char *json, const char *field, float *out_value)
{
    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor == '\0') {
        return false;
    }

    char *endptr = NULL;
    float value = strtof(cursor, &endptr);
    if (endptr == cursor) {
        return false;
    }

    *out_value = value;
    return true;
}

static bool config_manager_extract_uint32_field(const char *json,
                                                const char *field,
                                                uint32_t *out_value)
{
    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor == '\0') {
        return false;
    }

    char *endptr = NULL;
    unsigned long parsed = strtoul(cursor, &endptr, 10);
    if (endptr == cursor) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

static esp_err_t config_manager_validate_value(size_t index, float value)
{
    if (index >= s_register_count) {
        return ESP_ERR_INVALID_ARG;
    }

    const config_manager_register_descriptor_t *desc = &s_register_descriptors[index];
    if (value < desc->min_value || value > desc->max_value) {
        ESP_LOGW(TAG, "%s=%.3f outside range %.3f..%.3f", desc->key, value, desc->min_value, desc->max_value);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void config_manager_ensure_initialised(void)
{
    if (!s_registers_initialised) {
        config_manager_load_register_defaults();
    }

    if (!s_settings_loaded) {
        config_manager_load_persistent_settings();
    }

    if (s_config_length == 0) {
        if (config_manager_build_config_snapshot() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to build default configuration snapshot");
        }
    }
}

void config_manager_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void config_manager_init(void)
{
    config_manager_ensure_initialised();
    uart_bms_set_poll_interval_ms(s_uart_poll_interval_ms);
}

uint32_t config_manager_get_uart_poll_interval_ms(void)
{
    config_manager_ensure_initialised();
    return s_uart_poll_interval_ms;
}

esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms)
{
    config_manager_ensure_initialised();

    uint32_t clamped = config_manager_clamp_poll_interval(interval_ms);
    if (clamped == s_uart_poll_interval_ms) {
        uart_bms_set_poll_interval_ms(clamped);
        return ESP_OK;
    }

    s_uart_poll_interval_ms = clamped;
    uart_bms_set_poll_interval_ms(clamped);

    esp_err_t persist_err = config_manager_store_poll_interval(clamped);
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist UART poll interval: %s", esp_err_to_name(persist_err));
    }

    esp_err_t snapshot_err = config_manager_build_config_snapshot();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    }

    if (persist_err != ESP_OK) {
        return persist_err;
    }
    return snapshot_err;
}

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    if (s_config_length + 1 > buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(buffer, s_config_json, s_config_length + 1);
    if (out_length != NULL) {
        *out_length = s_config_length;
    }

    return ESP_OK;
}

esp_err_t config_manager_set_config_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    if (length == 0) {
        length = strlen(json);
    }

    if (length >= CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        ESP_LOGW(TAG, "Config payload too large: %u bytes", (unsigned)length);
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    memcpy(buffer, json, length);
    buffer[length] = '\0';

    uint32_t poll_interval = 0;
    if (!config_manager_extract_uint32_field(buffer,
                                             "\"uart_poll_interval_ms\"",
                                             &poll_interval)) {
        ESP_LOGW(TAG, "Config payload missing uart_poll_interval_ms");
        return ESP_ERR_INVALID_ARG;
    }

    return config_manager_set_uart_poll_interval_ms(poll_interval);
}

esp_err_t config_manager_get_registers_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    size_t offset = 0;
    if (!config_manager_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "{\"total\":%zu,\"registers\":[",
                                    s_register_count)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < s_register_count; ++i) {
        const config_manager_register_descriptor_t *desc = &s_register_descriptors[i];
        if (!config_manager_json_append(buffer,
                                        buffer_size,
                                        &offset,
                                        "%s{\"key\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\",\"address\":%u,"
                                        "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"value\":%.3f}",
                                        (i == 0) ? "" : ",",
                                        desc->key,
                                        desc->label,
                                        desc->unit,
                                        (unsigned)desc->address,
                                        desc->min_value,
                                        desc->max_value,
                                        desc->step,
                                        s_register_values[i])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!config_manager_json_append(buffer, buffer_size, &offset, "]}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}

esp_err_t config_manager_apply_register_update_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    if (length == 0) {
        length = strlen(json);
    }

    if (length >= CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    memcpy(buffer, json, length);
    buffer[length] = '\0';

    char key[CONFIG_MANAGER_MAX_REGISTER_KEY];
    if (!config_manager_extract_string_field(buffer, "\"key\"", key, sizeof(key))) {
        return ESP_ERR_INVALID_ARG;
    }

    float value = 0.0f;
    if (!config_manager_extract_float_field(buffer, "\"value\"", &value)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = 0;
    if (!config_manager_find_register(key, &index)) {
        ESP_LOGW(TAG, "Unknown register key %s", key);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t validation = config_manager_validate_value(index, value);
    if (validation != ESP_OK) {
        return validation;
    }

    const config_manager_register_descriptor_t *desc = &s_register_descriptors[index];
    if (desc->step > 0.0f) {
        float steps = (value - desc->min_value) / desc->step;
        float rounded = (steps >= 0.0f) ? (float)((int)(steps + 0.5f)) : (float)((int)(steps - 0.5f));
        value = desc->min_value + desc->step * rounded;
        if (value < desc->min_value) {
            value = desc->min_value;
        } else if (value > desc->max_value) {
            value = desc->max_value;
        }
    }

    s_register_values[index] = value;
    config_manager_publish_register_change(desc->key, value);
    return config_manager_build_config_snapshot();
}
