#include "config_manager.h"

#include <ctype.h>
#include <math.h>
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
#define CONFIG_MANAGER_REGISTER_KEY_PREFIX    "reg"
#define CONFIG_MANAGER_REGISTER_KEY_MAX       16

#define CONFIG_MANAGER_MQTT_URI_KEY       "mqtt_uri"
#define CONFIG_MANAGER_MQTT_USERNAME_KEY  "mqtt_user"
#define CONFIG_MANAGER_MQTT_PASSWORD_KEY  "mqtt_pass"
#define CONFIG_MANAGER_MQTT_KEEPALIVE_KEY "mqtt_keepalive"
#define CONFIG_MANAGER_MQTT_QOS_KEY       "mqtt_qos"
#define CONFIG_MANAGER_MQTT_RETAIN_KEY    "mqtt_retain"

#ifndef CONFIG_TINYBMS_MQTT_BROKER_URI
#define CONFIG_TINYBMS_MQTT_BROKER_URI "mqtt://localhost"
#endif

#ifndef CONFIG_TINYBMS_MQTT_USERNAME
#define CONFIG_TINYBMS_MQTT_USERNAME ""
#endif

#ifndef CONFIG_TINYBMS_MQTT_PASSWORD
#define CONFIG_TINYBMS_MQTT_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_MQTT_KEEPALIVE
#define CONFIG_TINYBMS_MQTT_KEEPALIVE 60
#endif

#ifndef CONFIG_TINYBMS_MQTT_DEFAULT_QOS
#define CONFIG_TINYBMS_MQTT_DEFAULT_QOS 1
#endif

#ifndef CONFIG_TINYBMS_MQTT_RETAIN_STATUS
#define CONFIG_TINYBMS_MQTT_RETAIN_STATUS 0
#endif

#define CONFIG_MANAGER_MQTT_DEFAULT_URI       CONFIG_TINYBMS_MQTT_BROKER_URI
#define CONFIG_MANAGER_MQTT_DEFAULT_USERNAME  CONFIG_TINYBMS_MQTT_USERNAME
#define CONFIG_MANAGER_MQTT_DEFAULT_PASSWORD  CONFIG_TINYBMS_MQTT_PASSWORD
#define CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE ((uint16_t)CONFIG_TINYBMS_MQTT_KEEPALIVE)
#define CONFIG_MANAGER_MQTT_DEFAULT_QOS       ((uint8_t)CONFIG_TINYBMS_MQTT_DEFAULT_QOS)
#define CONFIG_MANAGER_MQTT_DEFAULT_RETAIN    (CONFIG_TINYBMS_MQTT_RETAIN_STATUS != 0)

static void config_manager_make_register_key(uint16_t address, char *out_key, size_t out_size)
{
    if (out_key == NULL || out_size == 0) {
        return;
    }
    if (snprintf(out_key, out_size, CONFIG_MANAGER_REGISTER_KEY_PREFIX "%04X", (unsigned)address) >= (int)out_size) {
        out_key[out_size - 1] = '\0';
    }
}

typedef enum {
    CONFIG_MANAGER_ACCESS_RO = 0,
    CONFIG_MANAGER_ACCESS_WO,
    CONFIG_MANAGER_ACCESS_RW,
} config_manager_access_t;

typedef enum {
    CONFIG_MANAGER_VALUE_NUMERIC = 0,
    CONFIG_MANAGER_VALUE_ENUM,
} config_manager_value_class_t;

typedef struct {
    uint16_t value;
    const char *label;
} config_manager_enum_entry_t;

typedef struct {
    uint16_t address;
    const char *key;
    const char *label;
    const char *unit;
    const char *group;
    const char *comment;
    const char *type;
    config_manager_access_t access;
    float scale;
    uint8_t precision;
    bool has_min;
    uint16_t min_raw;
    bool has_max;
    uint16_t max_raw;
    float step_raw;
    uint16_t default_raw;
    config_manager_value_class_t value_class;
    const config_manager_enum_entry_t *enum_values;
    size_t enum_count;
} config_manager_register_descriptor_t;

#include "generated_tiny_rw_registers.inc"

static const char *TAG = "config_manager";

static mqtt_client_config_t s_mqtt_config = {
    .broker_uri = CONFIG_MANAGER_MQTT_DEFAULT_URI,
    .username = CONFIG_MANAGER_MQTT_DEFAULT_USERNAME,
    .password = CONFIG_MANAGER_MQTT_DEFAULT_PASSWORD,
    .keepalive_seconds = CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE,
    .default_qos = CONFIG_MANAGER_MQTT_DEFAULT_QOS,
    .retain_enabled = CONFIG_MANAGER_MQTT_DEFAULT_RETAIN,
};

static void config_manager_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    size_t copy_len = 0;
    while (copy_len + 1 < dest_size && src[copy_len] != '\0') {
        ++copy_len;
    }

    if (copy_len > 0) {
        memcpy(dest, src, copy_len);
    }
    dest[copy_len] = '\0';
}

static void config_manager_sanitise_mqtt_config(mqtt_client_config_t *config)
{
    if (config == NULL) {
        return;
    }

    if (config->keepalive_seconds == 0) {
        config->keepalive_seconds = CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE;
    }

    if (config->default_qos > 2U) {
        config->default_qos = 2U;
    }

    if (config->broker_uri[0] == '\0') {
        config_manager_copy_string(config->broker_uri,
                                   sizeof(config->broker_uri),
                                   CONFIG_MANAGER_MQTT_DEFAULT_URI);
    }
}

#ifdef ESP_PLATFORM
static void config_manager_load_mqtt_settings_from_nvs(nvs_handle_t handle)
{
    size_t buffer_size = sizeof(s_mqtt_config.broker_uri);
    esp_err_t err = nvs_get_str(handle, CONFIG_MANAGER_MQTT_URI_KEY, s_mqtt_config.broker_uri, &buffer_size);
    if (err != ESP_OK) {
        config_manager_copy_string(s_mqtt_config.broker_uri,
                                   sizeof(s_mqtt_config.broker_uri),
                                   CONFIG_MANAGER_MQTT_DEFAULT_URI);
    }

    buffer_size = sizeof(s_mqtt_config.username);
    err = nvs_get_str(handle, CONFIG_MANAGER_MQTT_USERNAME_KEY, s_mqtt_config.username, &buffer_size);
    if (err != ESP_OK) {
        config_manager_copy_string(s_mqtt_config.username,
                                   sizeof(s_mqtt_config.username),
                                   CONFIG_MANAGER_MQTT_DEFAULT_USERNAME);
    }

    buffer_size = sizeof(s_mqtt_config.password);
    err = nvs_get_str(handle, CONFIG_MANAGER_MQTT_PASSWORD_KEY, s_mqtt_config.password, &buffer_size);
    if (err != ESP_OK) {
        config_manager_copy_string(s_mqtt_config.password,
                                   sizeof(s_mqtt_config.password),
                                   CONFIG_MANAGER_MQTT_DEFAULT_PASSWORD);
    }

    uint16_t keepalive = 0U;
    err = nvs_get_u16(handle, CONFIG_MANAGER_MQTT_KEEPALIVE_KEY, &keepalive);
    if (err == ESP_OK) {
        s_mqtt_config.keepalive_seconds = keepalive;
    }

    uint8_t qos = 0U;
    err = nvs_get_u8(handle, CONFIG_MANAGER_MQTT_QOS_KEY, &qos);
    if (err == ESP_OK) {
        s_mqtt_config.default_qos = qos;
    }

    uint8_t retain = 0U;
    err = nvs_get_u8(handle, CONFIG_MANAGER_MQTT_RETAIN_KEY, &retain);
    if (err == ESP_OK) {
        s_mqtt_config.retain_enabled = (retain != 0U);
    }

    config_manager_sanitise_mqtt_config(&s_mqtt_config);
}
#else
static void config_manager_load_mqtt_settings_from_nvs(void)
{
    config_manager_sanitise_mqtt_config(&s_mqtt_config);
}
#endif

static event_bus_publish_fn_t s_event_publisher = NULL;
static char s_config_json[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
static size_t s_config_length = 0;
static uint16_t s_register_raw_values[s_register_count];
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

    config_manager_load_mqtt_settings_from_nvs(handle);
    nvs_close(handle);
#else
    s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    config_manager_load_mqtt_settings_from_nvs();
#endif

    for (size_t i = 0; i < s_register_count; ++i) {
        const config_manager_register_descriptor_t *desc = &s_register_descriptors[i];
        uint16_t stored_raw = 0;
        if (!config_manager_load_register_raw(desc->address, &stored_raw)) {
            continue;
        }

        if (desc->value_class == CONFIG_MANAGER_VALUE_ENUM) {
            bool found = false;
            for (size_t e = 0; e < desc->enum_count; ++e) {
                if (desc->enum_values[e].value == stored_raw) {
                    found = true;
                    break;
                }
            }
            if (found) {
                s_register_raw_values[i] = stored_raw;
            }
            continue;
        }

        uint16_t aligned = 0;
        if (config_manager_align_raw_value(desc, (float)stored_raw, &aligned) == ESP_OK) {
            s_register_raw_values[i] = aligned;
        }
    }
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

#ifdef ESP_PLATFORM
static esp_err_t config_manager_store_mqtt_config_to_nvs(const mqtt_client_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_URI_KEY, config->broker_uri);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_USERNAME_KEY, config->username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_PASSWORD_KEY, config->password);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, CONFIG_MANAGER_MQTT_KEEPALIVE_KEY, config->keepalive_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, CONFIG_MANAGER_MQTT_QOS_KEY, config->default_qos);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, CONFIG_MANAGER_MQTT_RETAIN_KEY, config->retain_enabled ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t config_manager_store_register_raw(uint16_t address, uint16_t raw_value)
{
    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[CONFIG_MANAGER_REGISTER_KEY_MAX];
    config_manager_make_register_key(address, key, sizeof(key));

    err = nvs_set_u16(handle, key, raw_value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool config_manager_load_register_raw(uint16_t address, uint16_t *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return false;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    char key[CONFIG_MANAGER_REGISTER_KEY_MAX];
    config_manager_make_register_key(address, key, sizeof(key));
    uint16_t value = 0;
    err = nvs_get_u16(handle, key, &value);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    *out_value = value;
    return true;
}
#else
static esp_err_t config_manager_store_mqtt_config_to_nvs(const mqtt_client_config_t *config)
{
    (void)config;
    return ESP_OK;
}

static esp_err_t config_manager_store_register_raw(uint16_t address, uint16_t raw_value)
{
    (void)address;
    (void)raw_value;
    return ESP_OK;
}

static bool config_manager_load_register_raw(uint16_t address, uint16_t *out_value)
{
    (void)address;
    (void)out_value;
    return false;
}
#endif

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

static void config_manager_publish_register_change(const config_manager_register_descriptor_t *desc,
                                                   uint16_t raw_value)
{
    if (s_event_publisher == NULL || desc == NULL) {
        return;
    }

    size_t slot = s_next_register_event;
    s_next_register_event = (s_next_register_event + 1) % CONFIG_MANAGER_REGISTER_EVENT_BUFFERS;

    char *payload = s_register_events[slot];
    float user_value = (desc->value_class == CONFIG_MANAGER_VALUE_ENUM)
                           ? (float)raw_value
                           : config_manager_raw_to_user(desc, raw_value);
    int precision = (desc->value_class == CONFIG_MANAGER_VALUE_ENUM) ? 0 : desc->precision;
    int written = snprintf(payload,
                           CONFIG_MANAGER_MAX_UPDATE_PAYLOAD,
                           "{\"type\":\"register_update\",\"key\":\"%s\",\"value\":%.*f,\"raw\":%u}",
                           desc->key,
                           precision,
                           user_value,
                           (unsigned)raw_value);
    if (written < 0 || written >= CONFIG_MANAGER_MAX_UPDATE_PAYLOAD) {
        ESP_LOGW(TAG, "Register update payload truncated for %s", desc->key);
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
        s_register_raw_values[i] = s_register_descriptors[i].default_raw;
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

static float config_manager_raw_to_user(const config_manager_register_descriptor_t *desc, uint16_t raw_value)
{
    if (desc == NULL) {
        return 0.0f;
    }
    return (float)raw_value * desc->scale;
}

static esp_err_t config_manager_align_raw_value(const config_manager_register_descriptor_t *desc,
                                                float requested_raw,
                                                uint16_t *out_raw)
{
    if (desc == NULL || out_raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float aligned_raw = requested_raw;
    if (desc->step_raw > 0.0f) {
        float base = desc->has_min ? (float)desc->min_raw : 0.0f;
        float steps = (aligned_raw - base) / desc->step_raw;
        float rounded = nearbyintf(steps);
        aligned_raw = base + desc->step_raw * rounded;
    }

    if (desc->has_min && aligned_raw < (float)desc->min_raw) {
        aligned_raw = (float)desc->min_raw;
    }
    if (desc->has_max && aligned_raw > (float)desc->max_raw) {
        aligned_raw = (float)desc->max_raw;
    }

    if (aligned_raw < 0.0f || aligned_raw > 65535.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_raw = (uint16_t)lrintf(aligned_raw);
    return ESP_OK;
}

static esp_err_t config_manager_convert_user_to_raw(const config_manager_register_descriptor_t *desc,
                                                    float user_value,
                                                    uint16_t *out_raw,
                                                    float *out_aligned_user)
{
    if (desc == NULL || out_raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (desc->access != CONFIG_MANAGER_ACCESS_RW) {
        return ESP_ERR_INVALID_STATE;
    }

    if (desc->value_class == CONFIG_MANAGER_VALUE_ENUM) {
        uint16_t candidate = (uint16_t)lrintf(user_value);
        for (size_t i = 0; i < desc->enum_count; ++i) {
            if (desc->enum_values[i].value == candidate) {
                *out_raw = candidate;
                if (out_aligned_user != NULL) {
                    *out_aligned_user = (float)candidate;
                }
                return ESP_OK;
            }
        }
        ESP_LOGW(TAG, "%s value %.3f does not match enum options", desc->key, user_value);
        return ESP_ERR_INVALID_ARG;
    }

    if (desc->scale <= 0.0f) {
        ESP_LOGW(TAG, "Register %s has invalid scale %.3f", desc->key, desc->scale);
        return ESP_ERR_INVALID_STATE;
    }

    float requested_raw = user_value / desc->scale;
    uint16_t raw_value = 0;
    esp_err_t err = config_manager_align_raw_value(desc, requested_raw, &raw_value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s unable to align %.3f -> raw", desc->key, user_value);
        return err;
    }

    if (desc->has_min && raw_value < desc->min_raw) {
        ESP_LOGW(TAG,
                 "%s raw %u below minimum %u",
                 desc->key,
                 (unsigned)raw_value,
                 (unsigned)desc->min_raw);
        return ESP_ERR_INVALID_ARG;
    }
    if (desc->has_max && raw_value > desc->max_raw) {
        ESP_LOGW(TAG,
                 "%s raw %u above maximum %u",
                 desc->key,
                 (unsigned)raw_value,
                 (unsigned)desc->max_raw);
        return ESP_ERR_INVALID_ARG;
    }

    *out_raw = raw_value;
    if (out_aligned_user != NULL) {
        *out_aligned_user = config_manager_raw_to_user(desc, raw_value);
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

const mqtt_client_config_t *config_manager_get_mqtt_client_config(void)
{
    config_manager_ensure_initialised();
    return &s_mqtt_config;
}

esp_err_t config_manager_set_mqtt_client_config(const mqtt_client_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    mqtt_client_config_t updated = s_mqtt_config;
    config_manager_copy_string(updated.broker_uri, sizeof(updated.broker_uri), config->broker_uri);
    config_manager_copy_string(updated.username, sizeof(updated.username), config->username);
    config_manager_copy_string(updated.password, sizeof(updated.password), config->password);
    updated.keepalive_seconds = (config->keepalive_seconds == 0U)
                                    ? CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE
                                    : config->keepalive_seconds;
    updated.default_qos = config->default_qos;
    updated.retain_enabled = config->retain_enabled;

    config_manager_sanitise_mqtt_config(&updated);

    esp_err_t err = config_manager_store_mqtt_config_to_nvs(&updated);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist MQTT configuration: %s", esp_err_to_name(err));
        return err;
    }

    s_mqtt_config = updated;
    config_manager_publish_config_snapshot();
    return ESP_OK;
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
        uint16_t raw_value = s_register_raw_values[i];
        bool is_enum = (desc->value_class == CONFIG_MANAGER_VALUE_ENUM);
        float user_value = is_enum ? (float)raw_value : config_manager_raw_to_user(desc, raw_value);
        float min_user = (desc->has_min && !is_enum) ? config_manager_raw_to_user(desc, desc->min_raw) : 0.0f;
        float max_user = (desc->has_max && !is_enum) ? config_manager_raw_to_user(desc, desc->max_raw) : 0.0f;
        float step_user = (!is_enum) ? desc->step_raw * desc->scale : 0.0f;
        float default_user = is_enum ? (float)desc->default_raw : config_manager_raw_to_user(desc, desc->default_raw);
        const char *access_str = "ro";
        if (desc->access == CONFIG_MANAGER_ACCESS_RW) {
            access_str = "rw";
        } else if (desc->access == CONFIG_MANAGER_ACCESS_WO) {
            access_str = "wo";
        }

        if (!config_manager_json_append(buffer,
                                        buffer_size,
                                        &offset,
                                        "%s{\"key\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\",\"group\":\"%s\","
                                        "\"type\":\"%s\",\"access\":\"%s\",\"address\":%u,\"scale\":%.6f,"
                                        "\"precision\":%u,\"value\":%.*f,\"raw\":%u,\"default\":%.*f",
                                        (i == 0) ? "" : ",",
                                        desc->key,
                                        desc->label != NULL ? desc->label : "",
                                        desc->unit != NULL ? desc->unit : "",
                                        desc->group != NULL ? desc->group : "",
                                        desc->type != NULL ? desc->type : "",
                                        access_str,
                                        (unsigned)desc->address,
                                        desc->scale,
                                        (unsigned)desc->precision,
                                        is_enum ? 0 : desc->precision,
                                        user_value,
                                        (unsigned)raw_value,
                                        is_enum ? 0 : desc->precision,
                                        default_user)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (!is_enum) {
            if (desc->has_min &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"min\":%.*f",
                                            desc->precision,
                                            min_user)) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (desc->has_max &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"max\":%.*f",
                                            desc->precision,
                                            max_user)) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (desc->step_raw > 0.0f &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"step\":%.*f",
                                            desc->precision,
                                            step_user)) {
                return ESP_ERR_INVALID_SIZE;
            }
        }

        if (desc->comment != NULL &&
            !config_manager_json_append(buffer,
                                        buffer_size,
                                        &offset,
                                        ",\"comment\":\"%s\"",
                                        desc->comment)) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (desc->enum_count > 0U) {
            if (!config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"enum\":[")) {
                return ESP_ERR_INVALID_SIZE;
            }
            for (size_t e = 0; e < desc->enum_count; ++e) {
                const config_manager_enum_entry_t *entry = &desc->enum_values[e];
                if (!config_manager_json_append(buffer,
                                                buffer_size,
                                                &offset,
                                                "%s{\"value\":%u,\"label\":\"%s\"}",
                                                (e == 0) ? "" : ",",
                                                (unsigned)entry->value,
                                                entry->label != NULL ? entry->label : "")) {
                    return ESP_ERR_INVALID_SIZE;
                }
            }
            if (!config_manager_json_append(buffer, buffer_size, &offset, "]")) {
                return ESP_ERR_INVALID_SIZE;
            }
        }

        if (!config_manager_json_append(buffer, buffer_size, &offset, "}")) {
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

    float requested_value = 0.0f;
    if (!config_manager_extract_float_field(buffer, "\"value\"", &requested_value)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = 0;
    if (!config_manager_find_register(key, &index)) {
        ESP_LOGW(TAG, "Unknown register key %s", key);
        return ESP_ERR_NOT_FOUND;
    }

    const config_manager_register_descriptor_t *desc = &s_register_descriptors[index];
    uint16_t raw_value = 0;
    esp_err_t conversion = config_manager_convert_user_to_raw(desc, requested_value, &raw_value, NULL);
    if (conversion != ESP_OK) {
        return conversion;
    }

    uint16_t readback_raw = raw_value;
    esp_err_t write_err = uart_bms_write_register(desc->address,
                                                  raw_value,
                                                  &readback_raw,
                                                  UART_BMS_RESPONSE_TIMEOUT_MS);
    if (write_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to write register %s (0x%04X): %s",
                 desc->key,
                 (unsigned)desc->address,
                 esp_err_to_name(write_err));
        return write_err;
    }

    s_register_raw_values[index] = readback_raw;
    config_manager_publish_register_change(desc, readback_raw);
#ifdef ESP_PLATFORM
    esp_err_t persist_err = config_manager_store_register_raw(desc->address, readback_raw);
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to persist register 0x%04X: %s",
                 (unsigned)desc->address,
                 esp_err_to_name(persist_err));
    }
#endif
    return config_manager_build_config_snapshot();
}
