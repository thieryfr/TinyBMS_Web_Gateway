/**
 * @file config_manager_json.c
 * @brief JSON serialization, deserialization, and configuration API
 *
 * This module handles all JSON operations for configuration management,
 * including parsing incoming JSON configuration, rendering configuration
 * snapshots, and providing the public JSON API for config access.
 */

#include "config_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "cJSON.h"

#include "esp_log.h"

#include "app_config.h"
#include "app_events.h"
#include "mqtt_topics.h"
#include "uart_bms.h"

#if CONFIG_TINYBMS_WIFI_ENABLE
void wifi_start_sta_mode(void) __attribute__((weak));
#endif

#define CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH 8U

static const char *TAG = "config_manager_json";

// Include register descriptors
#include "generated_tiny_rw_registers.inc"

// Forward declarations for functions from other modules
extern esp_err_t config_manager_lock(TickType_t timeout);
extern void config_manager_unlock(void);
extern void config_manager_ensure_initialised(void);
extern uint32_t config_manager_clamp_poll_interval(uint32_t interval_ms);
extern bool config_manager_find_register(const char *key, size_t *index_out);
extern float config_manager_raw_to_user(const config_manager_register_descriptor_t *desc, uint16_t raw_value);
extern esp_err_t config_manager_align_raw_value(const config_manager_register_descriptor_t *desc,
                                                 float requested_raw,
                                                 uint16_t *out_raw);
extern esp_err_t config_manager_convert_user_to_raw(const config_manager_register_descriptor_t *desc,
                                                     float user_value,
                                                     uint16_t *out_raw,
                                                     float *out_aligned_user);
extern const char *config_manager_effective_device_name_impl(void);
extern void config_manager_apply_ap_secret_if_needed(config_manager_wifi_settings_t *wifi);
extern void config_manager_update_topics_for_device_change(const char *old_name, const char *new_name);
extern esp_err_t config_manager_store_poll_interval(uint32_t interval_ms);
extern void config_manager_ensure_topics_loaded(void);
extern void config_manager_parse_mqtt_uri(const char *uri,
                                           char *out_scheme,
                                           size_t scheme_size,
                                           char *out_host,
                                           size_t host_size,
                                           uint16_t *out_port);

// Access to static variables from other modules
extern config_manager_device_settings_t s_device_settings;
extern config_manager_uart_pins_t s_uart_pins;
extern config_manager_wifi_settings_t s_wifi_settings;
extern config_manager_can_settings_t s_can_settings;
extern uint32_t s_uart_poll_interval_ms;
extern mqtt_client_config_t s_mqtt_config;
extern config_manager_mqtt_topics_t s_mqtt_topics;
extern uint16_t s_register_raw_values[];
extern bool s_config_file_loaded;
extern char s_config_json_full[];
extern size_t s_config_length_full;
extern char s_config_json_public[];
extern size_t s_config_length_public;
extern event_bus_publish_fn_t s_event_publisher;

// JSON utility functions
void config_manager_copy_string(char *dest, size_t dest_size, const char *src)
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

static const cJSON *config_manager_get_object(const cJSON *parent, const char *field)
{
    if (parent == NULL || field == NULL) {
        return NULL;
    }

    const cJSON *candidate = cJSON_GetObjectItemCaseSensitive(parent, field);
    return cJSON_IsObject(candidate) ? candidate : NULL;
}

static bool config_manager_copy_json_string(const cJSON *object,
                                            const char *field,
                                            char *dest,
                                            size_t dest_size)
{
    if (object == NULL || field == NULL || dest == NULL || dest_size == 0) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }

    config_manager_copy_string(dest, dest_size, item->valuestring);
    return true;
}

static bool config_manager_get_uint32_json(const cJSON *object,
                                           const char *field,
                                           uint32_t *out_value)
{
    if (object == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    double value = item->valuedouble;
    if (value < 0.0) {
        value = 0.0;
    }
    if (value > (double)UINT32_MAX) {
        value = (double)UINT32_MAX;
    }

    *out_value = (uint32_t)value;
    return true;
}

static bool config_manager_get_int32_json(const cJSON *object,
                                          const char *field,
                                          int32_t *out_value)
{
    if (object == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    double value = item->valuedouble;
    if (value < (double)INT32_MIN) {
        value = (double)INT32_MIN;
    }
    if (value > (double)INT32_MAX) {
        value = (double)INT32_MAX;
    }

    *out_value = (int32_t)value;
    return true;
}

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

static const char *config_manager_select_secret_value(const char *value, bool include_secrets)
{
    if (value == NULL) {
        return "";
    }
    return include_secrets ? value : config_manager_mask_secret(value);
}

static esp_err_t config_manager_render_config_snapshot_locked(bool include_secrets,
                                                             char *buffer,
                                                             size_t buffer_size,
                                                             size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = 0;
    }

    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char scheme[16];
    char host[MQTT_CLIENT_MAX_URI_LENGTH];
    uint16_t port = 0U;
    config_manager_parse_mqtt_uri(s_mqtt_config.broker_uri, scheme, sizeof(scheme), host, sizeof(host), &port);

    const char *device_name = config_manager_effective_device_name_impl();
    char version[16];
    (void)snprintf(version,
                   sizeof(version),
                   "%u.%u.%u",
                   APP_VERSION_MAJOR,
                   APP_VERSION_MINOR,
                   APP_VERSION_PATCH);

    esp_err_t result = ESP_OK;

#define CHECK_JSON(expr)               \
    do {                               \
        if ((expr) == NULL) {          \
            result = ESP_ERR_NO_MEM;   \
            goto cleanup;              \
        }                              \
    } while (0)

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    CHECK_JSON(cJSON_AddNumberToObject(root, "register_count", (double)s_register_count));
    CHECK_JSON(cJSON_AddNumberToObject(root, "uart_poll_interval_ms", (double)s_uart_poll_interval_ms));
    CHECK_JSON(cJSON_AddNumberToObject(root, "uart_poll_interval_min_ms", (double)UART_BMS_MIN_POLL_INTERVAL_MS));
    CHECK_JSON(cJSON_AddNumberToObject(root, "uart_poll_interval_max_ms", (double)UART_BMS_MAX_POLL_INTERVAL_MS));

    cJSON *device = cJSON_CreateObject();
    if (device == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(device, "name", device_name) == NULL ||
        cJSON_AddStringToObject(device, "version", version) == NULL) {
        cJSON_Delete(device);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(root, "device", device);

    cJSON *uart = cJSON_CreateObject();
    if (uart == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddNumberToObject(uart, "tx_gpio", s_uart_pins.tx_gpio) == NULL ||
        cJSON_AddNumberToObject(uart, "rx_gpio", s_uart_pins.rx_gpio) == NULL ||
        cJSON_AddNumberToObject(uart, "poll_interval_ms", (double)s_uart_poll_interval_ms) == NULL ||
        cJSON_AddNumberToObject(uart, "poll_interval_min_ms", (double)UART_BMS_MIN_POLL_INTERVAL_MS) == NULL ||
        cJSON_AddNumberToObject(uart, "poll_interval_max_ms", (double)UART_BMS_MAX_POLL_INTERVAL_MS) == NULL) {
        cJSON_Delete(uart);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(root, "uart", uart);

    cJSON *wifi = cJSON_CreateObject();
    if (wifi == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON *wifi_sta = cJSON_CreateObject();
    if (wifi_sta == NULL) {
        cJSON_Delete(wifi);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(wifi_sta, "ssid", s_wifi_settings.sta.ssid) == NULL ||
        cJSON_AddStringToObject(wifi_sta,
                                 "password",
                                 config_manager_select_secret_value(s_wifi_settings.sta.password, include_secrets)) == NULL ||
        cJSON_AddStringToObject(wifi_sta, "hostname", s_wifi_settings.sta.hostname) == NULL ||
        cJSON_AddNumberToObject(wifi_sta, "max_retry", (double)s_wifi_settings.sta.max_retry) == NULL) {
        cJSON_Delete(wifi_sta);
        cJSON_Delete(wifi);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(wifi, "sta", wifi_sta);

    cJSON *wifi_ap = cJSON_CreateObject();
    if (wifi_ap == NULL) {
        cJSON_Delete(wifi);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(wifi_ap, "ssid", s_wifi_settings.ap.ssid) == NULL ||
        cJSON_AddStringToObject(wifi_ap,
                                 "password",
                                 config_manager_select_secret_value(s_wifi_settings.ap.password, include_secrets)) == NULL ||
        cJSON_AddNumberToObject(wifi_ap, "channel", (double)s_wifi_settings.ap.channel) == NULL ||
        cJSON_AddNumberToObject(wifi_ap, "max_clients", (double)s_wifi_settings.ap.max_clients) == NULL) {
        cJSON_Delete(wifi_ap);
        cJSON_Delete(wifi);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(wifi, "ap", wifi_ap);
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON *can = cJSON_CreateObject();
    if (can == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    cJSON *twai = cJSON_CreateObject();
    if (twai == NULL) {
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddNumberToObject(twai, "tx_gpio", s_can_settings.twai.tx_gpio) == NULL ||
        cJSON_AddNumberToObject(twai, "rx_gpio", s_can_settings.twai.rx_gpio) == NULL) {
        cJSON_Delete(twai);
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(can, "twai", twai);

    cJSON *keepalive = cJSON_CreateObject();
    if (keepalive == NULL) {
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddNumberToObject(keepalive, "interval_ms", (double)s_can_settings.keepalive.interval_ms) == NULL ||
        cJSON_AddNumberToObject(keepalive, "timeout_ms", (double)s_can_settings.keepalive.timeout_ms) == NULL ||
        cJSON_AddNumberToObject(keepalive, "retry_ms", (double)s_can_settings.keepalive.retry_ms) == NULL) {
        cJSON_Delete(keepalive);
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(can, "keepalive", keepalive);

    cJSON *publisher = cJSON_CreateObject();
    if (publisher == NULL) {
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddNumberToObject(publisher, "period_ms", (double)s_can_settings.publisher.period_ms) == NULL) {
        cJSON_Delete(publisher);
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(can, "publisher", publisher);

    cJSON *identity = cJSON_CreateObject();
    if (identity == NULL) {
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(identity, "handshake_ascii", s_can_settings.identity.handshake_ascii) == NULL ||
        cJSON_AddStringToObject(identity, "manufacturer", s_can_settings.identity.manufacturer) == NULL ||
        cJSON_AddStringToObject(identity, "battery_name", s_can_settings.identity.battery_name) == NULL ||
        cJSON_AddStringToObject(identity, "battery_family", s_can_settings.identity.battery_family) == NULL ||
        cJSON_AddStringToObject(identity, "serial_number", s_can_settings.identity.serial_number) == NULL) {
        cJSON_Delete(identity);
        cJSON_Delete(can);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(can, "identity", identity);
    cJSON_AddItemToObject(root, "can", can);

    cJSON *mqtt = cJSON_CreateObject();
    if (mqtt == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(mqtt, "scheme", scheme) == NULL ||
        cJSON_AddStringToObject(mqtt, "broker_uri", s_mqtt_config.broker_uri) == NULL ||
        cJSON_AddStringToObject(mqtt, "host", host) == NULL ||
        cJSON_AddNumberToObject(mqtt, "port", (double)port) == NULL ||
        cJSON_AddStringToObject(mqtt, "username", s_mqtt_config.username) == NULL ||
        cJSON_AddStringToObject(mqtt,
                                 "password",
                                 config_manager_select_secret_value(s_mqtt_config.password, include_secrets)) == NULL ||
        cJSON_AddStringToObject(mqtt, "client_cert_path", s_mqtt_config.client_cert_path) == NULL ||
        cJSON_AddStringToObject(mqtt, "ca_cert_path", s_mqtt_config.ca_cert_path) == NULL ||
        cJSON_AddBoolToObject(mqtt, "verify_hostname", s_mqtt_config.verify_hostname) == NULL ||
        cJSON_AddNumberToObject(mqtt, "keepalive", (double)s_mqtt_config.keepalive_seconds) == NULL ||
        cJSON_AddNumberToObject(mqtt, "default_qos", (double)s_mqtt_config.default_qos) == NULL ||
        cJSON_AddBoolToObject(mqtt, "retain", s_mqtt_config.retain_enabled) == NULL) {
        cJSON_Delete(mqtt);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    cJSON *topics = cJSON_CreateObject();
    if (topics == NULL) {
        cJSON_Delete(mqtt);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (cJSON_AddStringToObject(topics, "status", s_mqtt_topics.status) == NULL ||
        cJSON_AddStringToObject(topics, "metrics", s_mqtt_topics.metrics) == NULL ||
        cJSON_AddStringToObject(topics, "config", s_mqtt_topics.config) == NULL ||
        cJSON_AddStringToObject(topics, "can_raw", s_mqtt_topics.can_raw) == NULL ||
        cJSON_AddStringToObject(topics, "can_decoded", s_mqtt_topics.can_decoded) == NULL ||
        cJSON_AddStringToObject(topics, "can_ready", s_mqtt_topics.can_ready) == NULL) {
        cJSON_Delete(topics);
        cJSON_Delete(mqtt);
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cJSON_AddItemToObject(mqtt, "topics", topics);
    cJSON_AddItemToObject(root, "mqtt", mqtt);

    buffer[0] = '\0';
    if (!cJSON_PrintPreallocated(root, buffer, buffer_size, false)) {
        char *json = cJSON_PrintUnformatted(root);
        if (json == NULL) {
            result = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        size_t length = strlen(json);
        if (length >= buffer_size) {
            cJSON_free(json);
            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        memcpy(buffer, json, length + 1U);
        if (out_length != NULL) {
            *out_length = length;
        }
        cJSON_free(json);
    } else {
        if (out_length != NULL) {
            *out_length = strlen(buffer);
        }
    }

cleanup:
    cJSON_Delete(root);
#undef CHECK_JSON
    return result;
}

static esp_err_t config_manager_apply_config_payload(const char *json,
                                                     size_t length,
                                                     bool persist,
                                                     bool apply_runtime)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length == 0U) {
        length = strlen(json);
    }
    if (length >= CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        ESP_LOGW(TAG, "Config payload too large: %u bytes", (unsigned)length);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == NULL) {
        const char *error = cJSON_GetErrorPtr();
        if (error != NULL) {
            ESP_LOGW(TAG, "Failed to parse configuration JSON near: %.32s", error);
        } else {
            ESP_LOGW(TAG, "Failed to parse configuration JSON");
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Configuration payload is not a JSON object");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_device_settings_t device = s_device_settings;
    config_manager_uart_pins_t uart_pins = s_uart_pins;
    config_manager_wifi_settings_t wifi = s_wifi_settings;
    config_manager_can_settings_t can = s_can_settings;
    uint32_t poll_interval = s_uart_poll_interval_ms;
    bool poll_interval_updated = false;
    bool sta_credentials_changed = false;

    const cJSON *device_obj = config_manager_get_object(root, "device");
    if (device_obj != NULL) {
        config_manager_copy_json_string(device_obj, "name", device.name, sizeof(device.name));
    }

    const cJSON *uart_obj = config_manager_get_object(root, "uart");
    if (uart_obj != NULL) {
        uint32_t poll = 0U;
        if (config_manager_get_uint32_json(uart_obj, "poll_interval_ms", &poll)) {
            poll_interval = config_manager_clamp_poll_interval(poll);
            poll_interval_updated = true;
        }

        int32_t gpio = 0;
        if (config_manager_get_int32_json(uart_obj, "tx_gpio", &gpio)) {
            if (gpio < -1) {
                gpio = -1;
            }
            if (gpio > 48) {
                gpio = 48;
            }
            uart_pins.tx_gpio = (int)gpio;
        }
        if (config_manager_get_int32_json(uart_obj, "rx_gpio", &gpio)) {
            if (gpio < -1) {
                gpio = -1;
            }
            if (gpio > 48) {
                gpio = 48;
            }
            uart_pins.rx_gpio = (int)gpio;
        }
    } else {
        uint32_t poll = 0U;
        if (config_manager_get_uint32_json(root, "uart_poll_interval_ms", &poll)) {
            poll_interval = config_manager_clamp_poll_interval(poll);
            poll_interval_updated = true;
        }
    }

    const cJSON *wifi_obj = config_manager_get_object(root, "wifi");
    if (wifi_obj != NULL) {
        const cJSON *sta_obj = config_manager_get_object(wifi_obj, "sta");
        if (sta_obj != NULL) {
            config_manager_copy_json_string(sta_obj, "ssid", wifi.sta.ssid, sizeof(wifi.sta.ssid));
            config_manager_copy_json_string(sta_obj, "password", wifi.sta.password, sizeof(wifi.sta.password));
            config_manager_copy_json_string(sta_obj, "hostname", wifi.sta.hostname, sizeof(wifi.sta.hostname));

            uint32_t max_retry = 0U;
            if (config_manager_get_uint32_json(sta_obj, "max_retry", &max_retry)) {
                if (max_retry > 255U) {
                    max_retry = 255U;
                }
                wifi.sta.max_retry = (uint8_t)max_retry;
            }
        }

        const cJSON *ap_obj = config_manager_get_object(wifi_obj, "ap");
        if (ap_obj != NULL) {
            config_manager_copy_json_string(ap_obj, "ssid", wifi.ap.ssid, sizeof(wifi.ap.ssid));
            config_manager_copy_json_string(ap_obj, "password", wifi.ap.password, sizeof(wifi.ap.password));

            uint32_t channel = 0U;
            if (config_manager_get_uint32_json(ap_obj, "channel", &channel)) {
                if (channel < 1U) {
                    channel = 1U;
                }
                if (channel > 13U) {
                    channel = 13U;
                }
                wifi.ap.channel = (uint8_t)channel;
            }

            uint32_t max_clients = 0U;
            if (config_manager_get_uint32_json(ap_obj, "max_clients", &max_clients)) {
                if (max_clients < 1U) {
                    max_clients = 1U;
                }
                if (max_clients > 10U) {
                    max_clients = 10U;
                }
                wifi.ap.max_clients = (uint8_t)max_clients;
            }
        }
    }

    sta_credentials_changed = (strcmp(wifi.sta.ssid, s_wifi_settings.sta.ssid) != 0) ||
                              (strcmp(wifi.sta.password, s_wifi_settings.sta.password) != 0);

    config_manager_apply_ap_secret_if_needed(&wifi);

    const cJSON *can_obj = config_manager_get_object(root, "can");
    if (can_obj != NULL) {
        const cJSON *twai_obj = config_manager_get_object(can_obj, "twai");
        if (twai_obj != NULL) {
            int32_t gpio = 0;
            if (config_manager_get_int32_json(twai_obj, "tx_gpio", &gpio)) {
                if (gpio < -1) {
                    gpio = -1;
                }
                if (gpio > 39) {
                    gpio = 39;
                }
                can.twai.tx_gpio = (int)gpio;
            }
            if (config_manager_get_int32_json(twai_obj, "rx_gpio", &gpio)) {
                if (gpio < -1) {
                    gpio = -1;
                }
                if (gpio > 39) {
                    gpio = 39;
                }
                can.twai.rx_gpio = (int)gpio;
            }
        }

        const cJSON *keepalive_obj = config_manager_get_object(can_obj, "keepalive");
        if (keepalive_obj != NULL) {
            uint32_t value = 0U;
            if (config_manager_get_uint32_json(keepalive_obj, "interval_ms", &value)) {
                if (value < 10U) {
                    value = 10U;
                }
                if (value > 600000U) {
                    value = 600000U;
                }
                can.keepalive.interval_ms = value;
            }
            if (config_manager_get_uint32_json(keepalive_obj, "timeout_ms", &value)) {
                if (value < 100U) {
                    value = 100U;
                }
                if (value > 600000U) {
                    value = 600000U;
                }
                can.keepalive.timeout_ms = value;
            }
            if (config_manager_get_uint32_json(keepalive_obj, "retry_ms", &value)) {
                if (value < 10U) {
                    value = 10U;
                }
                if (value > 600000U) {
                    value = 600000U;
                }
                can.keepalive.retry_ms = value;
            }
        }

        const cJSON *publisher_obj = config_manager_get_object(can_obj, "publisher");
        if (publisher_obj != NULL) {
            uint32_t value = 0U;
            if (config_manager_get_uint32_json(publisher_obj, "period_ms", &value)) {
                if (value > 600000U) {
                    value = 600000U;
                }
                can.publisher.period_ms = value;
            }
        }

        const cJSON *identity_obj = config_manager_get_object(can_obj, "identity");
        if (identity_obj != NULL) {
            config_manager_copy_json_string(identity_obj,
                                            "handshake_ascii",
                                            can.identity.handshake_ascii,
                                            sizeof(can.identity.handshake_ascii));
            config_manager_copy_json_string(identity_obj,
                                            "manufacturer",
                                            can.identity.manufacturer,
                                            sizeof(can.identity.manufacturer));
            config_manager_copy_json_string(identity_obj,
                                            "battery_name",
                                            can.identity.battery_name,
                                            sizeof(can.identity.battery_name));
            config_manager_copy_json_string(identity_obj,
                                            "battery_family",
                                            can.identity.battery_family,
                                            sizeof(can.identity.battery_family));
            config_manager_copy_json_string(identity_obj,
                                            "serial_number",
                                            can.identity.serial_number,
                                            sizeof(can.identity.serial_number));
        }
    }

    cJSON_Delete(root);

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    char previous_device_name[CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH];
    config_manager_copy_string(previous_device_name,
                               sizeof(previous_device_name),
                               config_manager_effective_device_name_impl());

    s_device_settings = device;
    s_uart_pins = uart_pins;
    s_wifi_settings = wifi;
    s_can_settings = can;

    const char *new_effective_name = config_manager_effective_device_name_impl();
    config_manager_update_topics_for_device_change(previous_device_name, new_effective_name);

    if (poll_interval_updated) {
        s_uart_poll_interval_ms = config_manager_clamp_poll_interval(poll_interval);

        // Persister d'abord, puis appliquer au runtime seulement si succès
        bool can_apply = !persist;  // Si pas de persistance demandée, on peut appliquer
        if (persist) {
            esp_err_t persist_err = config_manager_store_poll_interval(s_uart_poll_interval_ms);
            if (persist_err == ESP_OK) {
                can_apply = true;
                ESP_LOGI(TAG, "Persisted poll interval: %u ms", s_uart_poll_interval_ms);
            } else {
                ESP_LOGW(TAG,
                         "Failed to persist UART poll interval: %s, not applying to runtime",
                         esp_err_to_name(persist_err));
            }
        }

        if (apply_runtime && can_apply) {
            uart_bms_set_poll_interval_ms(s_uart_poll_interval_ms);
        }
    } else if (apply_runtime) {
        uart_bms_set_poll_interval_ms(s_uart_poll_interval_ms);
    }

    extern esp_err_t config_manager_build_config_snapshot_locked(void);
    extern void config_manager_publish_config_snapshot(void);
    extern esp_err_t config_manager_save_config_file(void);

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    }

    if (persist && snapshot_err == ESP_OK) {
        esp_err_t save_err = config_manager_save_config_file();
        if (save_err != ESP_OK) {
            snapshot_err = save_err;
        }
    }

    bool restart_sta = false;
#if CONFIG_TINYBMS_WIFI_ENABLE
    restart_sta = (apply_runtime && sta_credentials_changed && snapshot_err == ESP_OK);
#endif

    config_manager_unlock();

#if CONFIG_TINYBMS_WIFI_ENABLE
    if (restart_sta && wifi_start_sta_mode != NULL) {
        wifi_start_sta_mode();
    }
#endif

    return snapshot_err;
}

// Public API functions
esp_err_t config_manager_get_config_json(char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length,
                                         config_manager_snapshot_flags_t flags)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    bool include_secrets = ((flags & CONFIG_MANAGER_SNAPSHOT_INCLUDE_SECRETS) != 0);
    const char *source = include_secrets ? s_config_json_full : s_config_json_public;
    size_t length = include_secrets ? s_config_length_full : s_config_length_public;

    if (length + 1 > buffer_size) {
        config_manager_unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(buffer, source, length + 1);
    if (out_length != NULL) {
        *out_length = length;
    }

    config_manager_unlock();
    return ESP_OK;
}

esp_err_t config_manager_set_config_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    return config_manager_apply_config_payload(json, length, true, true);
}

esp_err_t config_manager_get_registers_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    esp_err_t result = ESP_OK;
    size_t offset = 0;

    if (!config_manager_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "{\"total\":%zu,\"registers\":[",
                                    s_register_count)) {
        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
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
                                        "%s{\"key\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\",\"group\":\"%s\","\
                                        "\"type\":\"%s\",\"access\":\"%s\",\"address\":%u,\"scale\":%.6f,"\
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
            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        if (!is_enum) {
            if (desc->has_min &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"min\":%.*f",
                                            desc->precision,
                                            min_user)) {
                result = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
            if (desc->has_max &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"max\":%.*f",
                                            desc->precision,
                                            max_user)) {
                result = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
            if (desc->step_raw > 0.0f &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"step\":%.*f",
                                            desc->precision,
                                            step_user)) {
                result = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
        }

        if (desc->comment != NULL &&
            !config_manager_json_append(buffer,
                                        buffer_size,
                                        &offset,
                                        ",\"comment\":\"%s\"",
                                        desc->comment)) {
            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        if (desc->enum_count > 0U) {
            if (!config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"enum\":[")) {
                result = ESP_ERR_INVALID_SIZE;
                goto cleanup;
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
                    result = ESP_ERR_INVALID_SIZE;
                    goto cleanup;
                }
            }
            if (!config_manager_json_append(buffer, buffer_size, &offset, "]")) {
                result = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
        }

        if (!config_manager_json_append(buffer, buffer_size, &offset, "}")) {
            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
    }

    if (!config_manager_json_append(buffer, buffer_size, &offset, "]}")) {
        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

cleanup:
    config_manager_unlock();
    if (result != ESP_OK && out_length != NULL) {
        *out_length = 0;
    }
    return result;
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

    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == NULL) {
        const char *error = cJSON_GetErrorPtr();
        if (error != NULL) {
            ESP_LOGW(TAG, "Failed to parse register update near: %.32s", error);
        } else {
            ESP_LOGW(TAG, "Failed to parse register update JSON");
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Register update payload is not a JSON object");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *key_node = cJSON_GetObjectItemCaseSensitive(root, "key");
    const cJSON *value_node = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (!cJSON_IsString(key_node) || key_node->valuestring == NULL || !cJSON_IsNumber(value_node)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    char key[CONFIG_MANAGER_MAX_REGISTER_KEY];
    config_manager_copy_string(key, sizeof(key), key_node->valuestring);
    float requested_value = (float)value_node->valuedouble;

    cJSON_Delete(root);

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

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    s_register_raw_values[index] = readback_raw;

#ifdef ESP_PLATFORM
    extern esp_err_t config_manager_store_register_raw(uint16_t address, uint16_t raw_value);
    esp_err_t persist_err = config_manager_store_register_raw(desc->address, readback_raw);
#endif

    extern esp_err_t config_manager_build_config_snapshot_locked(void);
    extern void config_manager_publish_register_change(const config_manager_register_descriptor_t *desc,
                                                        uint16_t raw_value);

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    config_manager_unlock();

    config_manager_publish_register_change(desc, readback_raw);
#ifdef ESP_PLATFORM
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to persist register 0x%04X: %s",
                 (unsigned)desc->address,
                 esp_err_to_name(persist_err));
    }
#endif
    return snapshot_err;
}
