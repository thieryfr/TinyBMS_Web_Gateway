#include "config_manager.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "cJSON.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_events.h"
#include "app_config.h"
#include "mqtt_topics.h"
#include "uart_bms.h"
#include "can_config_defaults.h"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#endif

#define CONFIG_MANAGER_REGISTER_EVENT_BUFFERS 4
#define CONFIG_MANAGER_MAX_UPDATE_PAYLOAD     192
#define CONFIG_MANAGER_MAX_REGISTER_KEY       32
#define CONFIG_MANAGER_NAMESPACE              "gateway_cfg"
#define CONFIG_MANAGER_POLL_KEY               "uart_poll"
#define CONFIG_MANAGER_REGISTER_KEY_PREFIX    "reg"
#define CONFIG_MANAGER_REGISTER_KEY_MAX       16

#define CONFIG_MANAGER_MQTT_URI_KEY          "mqtt_uri"
#define CONFIG_MANAGER_MQTT_USERNAME_KEY     "mqtt_user"
#define CONFIG_MANAGER_MQTT_PASSWORD_KEY     "mqtt_pass"
#define CONFIG_MANAGER_MQTT_KEEPALIVE_KEY    "mqtt_keepalive"
#define CONFIG_MANAGER_MQTT_QOS_KEY          "mqtt_qos"
#define CONFIG_MANAGER_MQTT_RETAIN_KEY       "mqtt_retain"
#define CONFIG_MANAGER_MQTT_TOPIC_STATUS_KEY "mqtt_t_stat"
#define CONFIG_MANAGER_MQTT_TOPIC_MET_KEY    "mqtt_t_met"
#define CONFIG_MANAGER_MQTT_TOPIC_CFG_KEY    "mqtt_t_cfg"
#define CONFIG_MANAGER_MQTT_TOPIC_RAW_KEY    "mqtt_t_crw"
#define CONFIG_MANAGER_MQTT_TOPIC_DEC_KEY    "mqtt_t_cdc"
#define CONFIG_MANAGER_MQTT_TOPIC_RDY_KEY    "mqtt_t_crd"

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

#define CONFIG_MANAGER_FS_BASE_PATH "/spiffs"
#define CONFIG_MANAGER_CONFIG_FILE  CONFIG_MANAGER_FS_BASE_PATH "/config.json"

#ifndef CONFIG_TINYBMS_WIFI_STA_SSID
#define CONFIG_TINYBMS_WIFI_STA_SSID ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_PASSWORD
#define CONFIG_TINYBMS_WIFI_STA_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_HOSTNAME
#define CONFIG_TINYBMS_WIFI_STA_HOSTNAME ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_MAX_RETRY
#define CONFIG_TINYBMS_WIFI_STA_MAX_RETRY 5
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_SSID
#define CONFIG_TINYBMS_WIFI_AP_SSID "TinyBMS-Gateway"
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_PASSWORD
#define CONFIG_TINYBMS_WIFI_AP_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_CHANNEL
#define CONFIG_TINYBMS_WIFI_AP_CHANNEL 1
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS
#define CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS 4
#endif

#ifndef CONFIG_TINYBMS_UART_TX_GPIO
#define CONFIG_TINYBMS_UART_TX_GPIO 37
#endif

#ifndef CONFIG_TINYBMS_UART_RX_GPIO
#define CONFIG_TINYBMS_UART_RX_GPIO 36
#endif

// CAN configuration defaults are now centralized in can_config_defaults.h

#ifndef CONFIG_TINYBMS_CAN_SERIAL_NUMBER
#define CONFIG_TINYBMS_CAN_SERIAL_NUMBER "TinyBMS-00000000"
#endif

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

#define CONFIG_MANAGER_LOCK_TIMEOUT pdMS_TO_TICKS(1000)

static mqtt_client_config_t s_mqtt_config = {
    .broker_uri = CONFIG_MANAGER_MQTT_DEFAULT_URI,
    .username = CONFIG_MANAGER_MQTT_DEFAULT_USERNAME,
    .password = CONFIG_MANAGER_MQTT_DEFAULT_PASSWORD,
    .keepalive_seconds = CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE,
    .default_qos = CONFIG_MANAGER_MQTT_DEFAULT_QOS,
    .retain_enabled = CONFIG_MANAGER_MQTT_DEFAULT_RETAIN,
};

static config_manager_mqtt_topics_t s_mqtt_topics = {0};
static bool s_mqtt_topics_loaded = false;

static config_manager_device_settings_t s_device_settings = {
    .name = APP_DEVICE_NAME,
};

static config_manager_uart_pins_t s_uart_pins = {
    .tx_gpio = CONFIG_TINYBMS_UART_TX_GPIO,
    .rx_gpio = CONFIG_TINYBMS_UART_RX_GPIO,
};

static config_manager_wifi_settings_t s_wifi_settings = {
    .sta = {
        .ssid = CONFIG_TINYBMS_WIFI_STA_SSID,
        .password = CONFIG_TINYBMS_WIFI_STA_PASSWORD,
        .hostname = CONFIG_TINYBMS_WIFI_STA_HOSTNAME,
        .max_retry = CONFIG_TINYBMS_WIFI_STA_MAX_RETRY,
    },
    .ap = {
        .ssid = CONFIG_TINYBMS_WIFI_AP_SSID,
        .password = CONFIG_TINYBMS_WIFI_AP_PASSWORD,
        .channel = CONFIG_TINYBMS_WIFI_AP_CHANNEL,
        .max_clients = CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS,
    },
};

static config_manager_can_settings_t s_can_settings = {
    .twai = {
        .tx_gpio = CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO,
        .rx_gpio = CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO,
    },
    .keepalive = {
        .interval_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS,
        .timeout_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS,
        .retry_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS,
    },
    .publisher = {
        .period_ms = CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS,
    },
    .identity = {
        .handshake_ascii = CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII,
        .manufacturer = CONFIG_TINYBMS_CAN_MANUFACTURER,
        .battery_name = CONFIG_TINYBMS_CAN_BATTERY_NAME,
        .battery_family = CONFIG_TINYBMS_CAN_BATTERY_FAMILY,
        .serial_number = CONFIG_TINYBMS_CAN_SERIAL_NUMBER,
    },
};

static bool s_config_file_loaded = false;
#ifdef ESP_PLATFORM
static bool s_spiffs_mounted = false;
#endif

static esp_err_t config_manager_apply_config_payload(const char *json,
                                                     size_t length,
                                                     bool persist,
                                                     bool apply_runtime);

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

static void config_manager_copy_topics(config_manager_mqtt_topics_t *dest,
                                       const config_manager_mqtt_topics_t *src)
{
    if (dest == NULL || src == NULL) {
        return;
    }

    config_manager_copy_string(dest->status, sizeof(dest->status), src->status);
    config_manager_copy_string(dest->metrics, sizeof(dest->metrics), src->metrics);
    config_manager_copy_string(dest->config, sizeof(dest->config), src->config);
    config_manager_copy_string(dest->can_raw, sizeof(dest->can_raw), src->can_raw);
    config_manager_copy_string(dest->can_decoded, sizeof(dest->can_decoded), src->can_decoded);
    config_manager_copy_string(dest->can_ready, sizeof(dest->can_ready), src->can_ready);
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

static const char *config_manager_effective_device_name(void)
{
    if (s_device_settings.name[0] != '\0') {
        return s_device_settings.name;
    }
    return APP_DEVICE_NAME;
}

static void config_manager_make_default_topics_for_name(const char *device_name,
                                                        config_manager_mqtt_topics_t *topics)
{
    if (topics == NULL) {
        return;
    }

    const char *name = (device_name != NULL && device_name[0] != '\0') ? device_name : APP_DEVICE_NAME;

    (void)snprintf(topics->status, sizeof(topics->status), MQTT_TOPIC_FMT_STATUS, name);
    (void)snprintf(topics->metrics, sizeof(topics->metrics), MQTT_TOPIC_FMT_METRICS, name);
    (void)snprintf(topics->config, sizeof(topics->config), MQTT_TOPIC_FMT_CONFIG, name);
    (void)snprintf(topics->can_raw, sizeof(topics->can_raw), MQTT_TOPIC_FMT_CAN_STREAM, name, "raw");
    (void)snprintf(topics->can_decoded, sizeof(topics->can_decoded), MQTT_TOPIC_FMT_CAN_STREAM, name, "decoded");
    (void)snprintf(topics->can_ready, sizeof(topics->can_ready), MQTT_TOPIC_FMT_CAN_STREAM, name, "ready");
}

static void config_manager_update_topics_for_device_change(const char *old_name, const char *new_name)
{
    if (old_name == NULL || new_name == NULL || strcmp(old_name, new_name) == 0) {
        return;
    }

    config_manager_mqtt_topics_t old_defaults = {0};
    config_manager_mqtt_topics_t new_defaults = {0};
    config_manager_make_default_topics_for_name(old_name, &old_defaults);
    config_manager_make_default_topics_for_name(new_name, &new_defaults);

    bool updated = false;
    if (strcmp(s_mqtt_topics.status, old_defaults.status) == 0) {
        config_manager_copy_string(s_mqtt_topics.status, sizeof(s_mqtt_topics.status), new_defaults.status);
        updated = true;
    }
    if (strcmp(s_mqtt_topics.metrics, old_defaults.metrics) == 0) {
        config_manager_copy_string(s_mqtt_topics.metrics, sizeof(s_mqtt_topics.metrics), new_defaults.metrics);
        updated = true;
    }
    if (strcmp(s_mqtt_topics.config, old_defaults.config) == 0) {
        config_manager_copy_string(s_mqtt_topics.config, sizeof(s_mqtt_topics.config), new_defaults.config);
        updated = true;
    }
    if (strcmp(s_mqtt_topics.can_raw, old_defaults.can_raw) == 0) {
        config_manager_copy_string(s_mqtt_topics.can_raw, sizeof(s_mqtt_topics.can_raw), new_defaults.can_raw);
        updated = true;
    }
    if (strcmp(s_mqtt_topics.can_decoded, old_defaults.can_decoded) == 0) {
        config_manager_copy_string(s_mqtt_topics.can_decoded, sizeof(s_mqtt_topics.can_decoded), new_defaults.can_decoded);
        updated = true;
    }
    if (strcmp(s_mqtt_topics.can_ready, old_defaults.can_ready) == 0) {
        config_manager_copy_string(s_mqtt_topics.can_ready, sizeof(s_mqtt_topics.can_ready), new_defaults.can_ready);
        updated = true;
    }

    if (updated) {
        config_manager_sanitise_mqtt_topics(&s_mqtt_topics);
        esp_err_t err = config_manager_store_mqtt_topics_to_nvs(&s_mqtt_topics);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist MQTT topics after device rename: %s", esp_err_to_name(err));
        }
    }
}

static void config_manager_reset_mqtt_topics(void)
{
    config_manager_make_default_topics_for_name(config_manager_effective_device_name(), &s_mqtt_topics);
}

static void config_manager_sanitise_mqtt_topics(config_manager_mqtt_topics_t *topics)
{
    if (topics == NULL) {
        return;
    }

    config_manager_copy_string(topics->status, sizeof(topics->status), topics->status);
    config_manager_copy_string(topics->metrics, sizeof(topics->metrics), topics->metrics);
    config_manager_copy_string(topics->config, sizeof(topics->config), topics->config);
    config_manager_copy_string(topics->can_raw, sizeof(topics->can_raw), topics->can_raw);
    config_manager_copy_string(topics->can_decoded, sizeof(topics->can_decoded), topics->can_decoded);
    config_manager_copy_string(topics->can_ready, sizeof(topics->can_ready), topics->can_ready);
}


static void config_manager_ensure_topics_loaded(void)
{
    if (!s_mqtt_topics_loaded) {
        config_manager_reset_mqtt_topics();
        s_mqtt_topics_loaded = true;
    }
}

static void config_manager_lowercase(char *value)
{
    if (value == NULL) {
        return;
    }

    for (size_t i = 0; value[i] != '\0'; ++i) {
        value[i] = (char)tolower((unsigned char)value[i]);
    }
}

static uint16_t config_manager_default_port_for_scheme(const char *scheme)
{
    if (scheme != NULL && strcmp(scheme, "mqtts") == 0) {
        return 8883U;
    }
    return 1883U;
}

static void config_manager_parse_mqtt_uri(const char *uri,
                                          char *out_scheme,
                                          size_t scheme_size,
                                          char *out_host,
                                          size_t host_size,
                                          uint16_t *out_port)
{
    if (out_scheme != NULL && scheme_size > 0) {
        out_scheme[0] = '\0';
    }
    if (out_host != NULL && host_size > 0) {
        out_host[0] = '\0';
    }
    if (out_port != NULL) {
        *out_port = 1883U;
    }

    char scheme_buffer[16] = "mqtt";
    const char *authority = uri;
    if (uri != NULL) {
        const char *sep = strstr(uri, "://");
        if (sep != NULL) {
            size_t len = (size_t)(sep - uri);
            if (len >= sizeof(scheme_buffer)) {
                len = sizeof(scheme_buffer) - 1U;
            }
            memcpy(scheme_buffer, uri, len);
            scheme_buffer[len] = '\0';
            authority = sep + 3;
        }
    }

    config_manager_lowercase(scheme_buffer);
    if (out_scheme != NULL && scheme_size > 0) {
        config_manager_copy_string(out_scheme, scheme_size, scheme_buffer);
    }

    uint16_t port = config_manager_default_port_for_scheme(scheme_buffer);
    if (authority == NULL) {
        if (out_port != NULL) {
            *out_port = port;
        }
        return;
    }

    const char *path = strpbrk(authority, "/?");
    size_t length = (path != NULL) ? (size_t)(path - authority) : strlen(authority);
    if (length == 0) {
        if (out_port != NULL) {
            *out_port = port;
        }
        return;
    }

    char host_buffer[MQTT_CLIENT_MAX_URI_LENGTH];
    if (length >= sizeof(host_buffer)) {
        length = sizeof(host_buffer) - 1U;
    }
    memcpy(host_buffer, authority, length);
    host_buffer[length] = '\0';

    char *colon = strrchr(host_buffer, ':');
    if (colon != NULL) {
        *colon = '\0';
        ++colon;
        char *endptr = NULL;
        unsigned long parsed = strtoul(colon, &endptr, 10);
        if (endptr != colon && parsed <= UINT16_MAX) {
            port = (uint16_t)parsed;
        }
    }

    if (out_host != NULL && host_size > 0) {
        config_manager_copy_string(out_host, host_size, host_buffer);
    }
    if (out_port != NULL) {
        *out_port = port;
    }
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
    config_manager_ensure_topics_loaded();

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

    buffer_size = sizeof(s_mqtt_topics.status);
    err = nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_STATUS_KEY, s_mqtt_topics.status, &buffer_size);
    if (err != ESP_OK) {
        config_manager_reset_mqtt_topics();
    }

    buffer_size = sizeof(s_mqtt_topics.metrics);
    if (nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_MET_KEY, s_mqtt_topics.metrics, &buffer_size) != ESP_OK) {
        config_manager_copy_string(s_mqtt_topics.metrics,
                                   sizeof(s_mqtt_topics.metrics),
                                   s_mqtt_topics.metrics);
    }

    buffer_size = sizeof(s_mqtt_topics.config);
    if (nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_CFG_KEY, s_mqtt_topics.config, &buffer_size) != ESP_OK) {
        config_manager_copy_string(s_mqtt_topics.config,
                                   sizeof(s_mqtt_topics.config),
                                   s_mqtt_topics.config);
    }

    buffer_size = sizeof(s_mqtt_topics.can_raw);
    if (nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_RAW_KEY, s_mqtt_topics.can_raw, &buffer_size) != ESP_OK) {
        config_manager_copy_string(s_mqtt_topics.can_raw,
                                   sizeof(s_mqtt_topics.can_raw),
                                   s_mqtt_topics.can_raw);
    }

    buffer_size = sizeof(s_mqtt_topics.can_decoded);
    if (nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_DEC_KEY, s_mqtt_topics.can_decoded, &buffer_size) != ESP_OK) {
        config_manager_copy_string(s_mqtt_topics.can_decoded,
                                   sizeof(s_mqtt_topics.can_decoded),
                                   s_mqtt_topics.can_decoded);
    }

    buffer_size = sizeof(s_mqtt_topics.can_ready);
    if (nvs_get_str(handle, CONFIG_MANAGER_MQTT_TOPIC_RDY_KEY, s_mqtt_topics.can_ready, &buffer_size) != ESP_OK) {
        config_manager_copy_string(s_mqtt_topics.can_ready,
                                   sizeof(s_mqtt_topics.can_ready),
                                   s_mqtt_topics.can_ready);
    }

    config_manager_sanitise_mqtt_config(&s_mqtt_config);
    config_manager_sanitise_mqtt_topics(&s_mqtt_topics);
}
#else
static void config_manager_load_mqtt_settings_from_nvs(void)
{
    config_manager_ensure_topics_loaded();
    config_manager_sanitise_mqtt_config(&s_mqtt_config);
    config_manager_sanitise_mqtt_topics(&s_mqtt_topics);
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

// Mutex to protect access to global configuration state
// NOTE: Currently protects write operations (setters) only.
// TODO: Full thread safety requires protecting all config structure access
static SemaphoreHandle_t s_config_mutex = NULL;

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

    esp_err_t file_err = config_manager_load_config_file(false);
    if (file_err != ESP_OK && file_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load configuration file: %s", esp_err_to_name(file_err));
    }

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
static esp_err_t config_manager_mount_spiffs(void)
{
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_MANAGER_FS_BASE_PATH,
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        s_spiffs_mounted = true;
        return ESP_OK;
    }

    if (err == ESP_OK) {
        s_spiffs_mounted = true;
    }
    return err;
}
#endif

static esp_err_t config_manager_save_config_file(void)
{
#ifdef ESP_PLATFORM
    esp_err_t mount_err = config_manager_mount_spiffs();
    if (mount_err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to mount SPIFFS for config save: %s", esp_err_to_name(mount_err));
        return mount_err;
    }
#endif

    FILE *file = fopen(CONFIG_MANAGER_CONFIG_FILE, "w");
    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to open %s for writing: errno=%d", CONFIG_MANAGER_CONFIG_FILE, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(s_config_json, 1, s_config_length, file);
    int flush_result = fflush(file);
    int close_result = fclose(file);
    if (written != s_config_length || flush_result != 0 || close_result != 0) {
        ESP_LOGW(TAG,
                 "Failed to write configuration file (written=%zu expected=%zu errno=%d)",
                 written,
                 s_config_length,
                 errno);
        return ESP_FAIL;
    }

    s_config_file_loaded = true;
    return ESP_OK;
}

static esp_err_t config_manager_load_config_file(bool apply_runtime)
{
#ifdef ESP_PLATFORM
    esp_err_t mount_err = config_manager_mount_spiffs();
    if (mount_err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to mount SPIFFS for config load: %s", esp_err_to_name(mount_err));
        return mount_err;
    }
#endif

    FILE *file = fopen(CONFIG_MANAGER_CONFIG_FILE, "r");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1U, file);
    fclose(file);
    if (read == 0U) {
        ESP_LOGW(TAG, "Configuration file %s is empty", CONFIG_MANAGER_CONFIG_FILE);
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[read] = '\0';
    esp_err_t err = config_manager_apply_config_payload(buffer, read, false, apply_runtime);
    if (err == ESP_OK) {
        s_config_file_loaded = true;
    }
    return err;
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

    // Vérifier tous les set avant commit (transaction atomique)
    bool all_ok = true;

    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_URI_KEY, config->broker_uri) == ESP_OK);
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_USERNAME_KEY, config->username) == ESP_OK);
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_PASSWORD_KEY, config->password) == ESP_OK);
    all_ok &= (nvs_set_u16(handle, CONFIG_MANAGER_MQTT_KEEPALIVE_KEY, config->keepalive_seconds) == ESP_OK);
    all_ok &= (nvs_set_u8(handle, CONFIG_MANAGER_MQTT_QOS_KEY, config->default_qos) == ESP_OK);
    all_ok &= (nvs_set_u8(handle, CONFIG_MANAGER_MQTT_RETAIN_KEY, config->retain_enabled ? 1U : 0U) == ESP_OK);

    if (!all_ok) {
        ESP_LOGE(TAG, "Failed to set one or more MQTT config values");
        nvs_close(handle);
        return ESP_FAIL;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t config_manager_store_mqtt_topics_to_nvs(const config_manager_mqtt_topics_t *topics)
{
    if (topics == NULL) {
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

    err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_STATUS_KEY, topics->status);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_MET_KEY, topics->metrics);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_CFG_KEY, topics->config);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_RAW_KEY, topics->can_raw);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_DEC_KEY, topics->can_decoded);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, CONFIG_MANAGER_MQTT_TOPIC_RDY_KEY, topics->can_ready);
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

static esp_err_t config_manager_store_mqtt_topics_to_nvs(const config_manager_mqtt_topics_t *topics)
{
    (void)topics;
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
        ESP_LOGW(TAG, "Failed to publish register update for %s", desc->key);
    }
}

static esp_err_t config_manager_build_config_snapshot(void)
{
    config_manager_ensure_topics_loaded();

    char scheme[16];
    char host[MQTT_CLIENT_MAX_URI_LENGTH];
    uint16_t port = 0U;
    config_manager_parse_mqtt_uri(s_mqtt_config.broker_uri, scheme, sizeof(scheme), host, sizeof(host), &port);

    size_t offset = 0;
    const char *device_name = config_manager_effective_device_name();
    char version[16];
    (void)snprintf(version,
                   sizeof(version),
                   "%u.%u.%u",
                   APP_VERSION_MAJOR,
                   APP_VERSION_MINOR,
                   APP_VERSION_PATCH);

    const config_manager_uart_pins_t *uart = &s_uart_pins;
    const config_manager_wifi_settings_t *wifi = &s_wifi_settings;
    const config_manager_can_settings_t *can = &s_can_settings;

    if (!config_manager_json_append(s_config_json, sizeof(s_config_json), &offset, "{")) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    "\"register_count\":%zu,\"uart_poll_interval_ms\":%u,"
                                    "\"uart_poll_interval_min_ms\":%u,\"uart_poll_interval_max_ms\":%u",
                                    s_register_count,
                                    (unsigned)s_uart_poll_interval_ms,
                                    (unsigned)UART_BMS_MIN_POLL_INTERVAL_MS,
                                    (unsigned)UART_BMS_MAX_POLL_INTERVAL_MS)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    ",\"device\":{\"name\":\"%s\",\"version\":\"%s\"}",
                                    device_name,
                                    version)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    ",\"uart\":{\"tx_gpio\":%d,\"rx_gpio\":%d,\"poll_interval_ms\":%u,"
                                    "\"poll_interval_min_ms\":%u,\"poll_interval_max_ms\":%u}",
                                    uart->tx_gpio,
                                    uart->rx_gpio,
                                    (unsigned)s_uart_poll_interval_ms,
                                    (unsigned)UART_BMS_MIN_POLL_INTERVAL_MS,
                                    (unsigned)UART_BMS_MAX_POLL_INTERVAL_MS)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    ",\"wifi\":{\"sta\":{\"ssid\":\"%s\",\"password\":\"%s\",\"hostname\":\"%s\",\"max_retry\":%u},"
                                    "\"ap\":{\"ssid\":\"%s\",\"password\":\"%s\",\"channel\":%u,\"max_clients\":%u}}",
                                    wifi->sta.ssid,
                                    wifi->sta.password,
                                    wifi->sta.hostname,
                                    (unsigned)wifi->sta.max_retry,
                                    wifi->ap.ssid,
                                    wifi->ap.password,
                                    (unsigned)wifi->ap.channel,
                                    (unsigned)wifi->ap.max_clients)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    ",\"can\":{\"twai\":{\"tx_gpio\":%d,\"rx_gpio\":%d},"
                                    "\"keepalive\":{\"interval_ms\":%u,\"timeout_ms\":%u,\"retry_ms\":%u},"
                                    "\"publisher\":{\"period_ms\":%u},"
                                    "\"identity\":{\"handshake_ascii\":\"%s\",\"manufacturer\":\"%s\",\"battery_name\":\"%s\","
                                    "\"battery_family\":\"%s\",\"serial_number\":\"%s\"}}",
                                    can->twai.tx_gpio,
                                    can->twai.rx_gpio,
                                    (unsigned)can->keepalive.interval_ms,
                                    (unsigned)can->keepalive.timeout_ms,
                                    (unsigned)can->keepalive.retry_ms,
                                    (unsigned)can->publisher.period_ms,
                                    can->identity.handshake_ascii,
                                    can->identity.manufacturer,
                                    can->identity.battery_name,
                                    can->identity.battery_family,
                                    can->identity.serial_number)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json,
                                    sizeof(s_config_json),
                                    &offset,
                                    ",\"mqtt\":{\"scheme\":\"%s\",\"broker_uri\":\"%s\",\"host\":\"%s\",\"port\":%u,"
                                    "\"username\":\"%s\",\"password\":\"%s\",\"keepalive\":%u,\"default_qos\":%u,\"
                                    "\"retain\":%s,\"topics\":{\"status\":\"%s\",\"metrics\":\"%s\",\"config\":\"%s\","
                                    "\"can_raw\":\"%s\",\"can_decoded\":\"%s\",\"can_ready\":\"%s\"}}",
                                    scheme,
                                    s_mqtt_config.broker_uri,
                                    host,
                                    (unsigned)port,
                                    s_mqtt_config.username,
                                    s_mqtt_config.password,
                                    (unsigned)s_mqtt_config.keepalive_seconds,
                                    (unsigned)s_mqtt_config.default_qos,
                                    s_mqtt_config.retain_enabled ? "true" : "false",
                                    s_mqtt_topics.status,
                                    s_mqtt_topics.metrics,
                                    s_mqtt_topics.config,
                                    s_mqtt_topics.can_raw,
                                    s_mqtt_topics.can_decoded,
                                    s_mqtt_topics.can_ready)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!config_manager_json_append(s_config_json, sizeof(s_config_json), &offset, "}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_config_length = offset;
    return ESP_OK;
}

static void config_manager_load_register_defaults(void)
{
    for (size_t i = 0; i < s_register_count; ++i) {
        s_register_raw_values[i] = s_register_descriptors[i].default_raw;
    }
    s_registers_initialised = true;
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

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (lock_err != ESP_OK) {
        cJSON_Delete(root);
        return lock_err;
    }

    esp_err_t result = ESP_OK;

    config_manager_device_settings_t device = s_device_settings;
    config_manager_uart_pins_t uart_pins = s_uart_pins;
    config_manager_wifi_settings_t wifi = s_wifi_settings;
    config_manager_can_settings_t can = s_can_settings;
    uint32_t poll_interval = s_uart_poll_interval_ms;
    bool poll_interval_updated = false;

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

    char previous_device_name[CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH];
    config_manager_copy_string(previous_device_name,
                               sizeof(previous_device_name),
                               config_manager_effective_device_name());

    s_device_settings = device;
    s_uart_pins = uart_pins;
    s_wifi_settings = wifi;
    s_can_settings = can;

    const char *new_effective_name = config_manager_effective_device_name();
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

    esp_err_t snapshot_err = config_manager_build_config_snapshot();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    }

    if (persist && snapshot_err == ESP_OK) {
        esp_err_t save_err = config_manager_save_config_file();
        if (save_err != ESP_OK) {
            snapshot_err = save_err;
        }
    }

    result = snapshot_err;

exit:
    config_manager_unlock_mutex();
    cJSON_Delete(root);
    return result;
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
    // Initialize mutex on first call (thread-safe in FreeRTOS)
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (s_config_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create config mutex");
        }
    }

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

static esp_err_t config_manager_lock_mutex(TickType_t timeout)
{
    if (s_config_mutex == NULL) {
        ESP_LOGE(TAG, "Config mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_config_mutex, timeout) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire config mutex");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void config_manager_unlock_mutex(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
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

    if (config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT) != ESP_OK) {
        return s_uart_poll_interval_ms;
    }

    uint32_t interval = s_uart_poll_interval_ms;
    config_manager_unlock_mutex();
    return interval;
}

esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    uint32_t clamped = config_manager_clamp_poll_interval(interval_ms);
    if (clamped == s_uart_poll_interval_ms) {
        config_manager_unlock_mutex();
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
        if (persist_err == ESP_OK && s_config_file_loaded) {
            esp_err_t save_err = config_manager_save_config_file();
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to update configuration file: %s", esp_err_to_name(save_err));
            }
        }
    }

    config_manager_unlock_mutex();

    if (persist_err != ESP_OK) {
        return persist_err;
    }
    return snapshot_err;
}

esp_err_t config_manager_get_uart_pins(config_manager_uart_pins_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_uart_pins;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_get_mqtt_client_config(mqtt_client_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_mqtt_config;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_set_mqtt_client_config(const mqtt_client_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

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

    esp_err_t persist_err = config_manager_store_mqtt_config_to_nvs(&updated);
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist MQTT configuration: %s", esp_err_to_name(persist_err));
        config_manager_unlock_mutex();
        return persist_err;
    }

    s_mqtt_config = updated;

    esp_err_t snapshot_err = config_manager_build_config_snapshot();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    } else {
        ESP_LOGW(TAG, "Failed to rebuild configuration snapshot: %s", esp_err_to_name(snapshot_err));
    }
    config_manager_unlock_mutex();
    return snapshot_err;
}

esp_err_t config_manager_get_mqtt_topics(config_manager_mqtt_topics_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_mqtt_topics;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_set_mqtt_topics(const config_manager_mqtt_topics_t *topics)
{
    if (topics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    config_manager_mqtt_topics_t updated = s_mqtt_topics;
    config_manager_copy_topics(&updated, topics);
    config_manager_sanitise_mqtt_topics(&updated);

    esp_err_t err = config_manager_store_mqtt_topics_to_nvs(&updated);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist MQTT topics: %s", esp_err_to_name(err));
        config_manager_unlock_mutex();
        return err;
    }

    s_mqtt_topics = updated;

    esp_err_t snapshot_err = config_manager_build_config_snapshot();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    } else {
        ESP_LOGW(TAG, "Failed to rebuild configuration snapshot after topic update: %s", esp_err_to_name(snapshot_err));
    }
    config_manager_unlock_mutex();
    return snapshot_err;
}

esp_err_t config_manager_get_config_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    if (s_config_length + 1 > buffer_size) {
        err = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    memcpy(buffer, s_config_json, s_config_length + 1);
    if (out_length != NULL) {
        *out_length = s_config_length;
    }

exit:
    config_manager_unlock_mutex();
    return err;
}

esp_err_t config_manager_set_config_json(const char *json, size_t length)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    return config_manager_apply_config_payload(json, length, true, true);
}

esp_err_t config_manager_get_device_settings(config_manager_device_settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_device_settings;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_get_device_name(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    const char *name = config_manager_effective_device_name();
    size_t required = strnlen(name, CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH);
    config_manager_copy_string(buffer, buffer_size, name);
    if (required + 1 > buffer_size) {
        err = ESP_ERR_INVALID_SIZE;
    }

    config_manager_unlock_mutex();
    return err;
}

esp_err_t config_manager_get_wifi_settings(config_manager_wifi_settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_wifi_settings;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_get_can_settings(config_manager_can_settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (err != ESP_OK) {
        return err;
    }

    *out = s_can_settings;
    config_manager_unlock_mutex();
    return ESP_OK;
}

esp_err_t config_manager_get_registers_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
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
        goto exit;
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
            result = ESP_ERR_INVALID_SIZE;
            goto exit;
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
                goto exit;
            }
            if (desc->has_max &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"max\":%.*f",
                                            desc->precision,
                                            max_user)) {
                result = ESP_ERR_INVALID_SIZE;
                goto exit;
            }
            if (desc->step_raw > 0.0f &&
                !config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"step\":%.*f",
                                            desc->precision,
                                            step_user)) {
                result = ESP_ERR_INVALID_SIZE;
                goto exit;
            }
        }

        if (desc->comment != NULL &&
            !config_manager_json_append(buffer,
                                        buffer_size,
                                        &offset,
                                        ",\"comment\":\"%s\"",
                                        desc->comment)) {
            result = ESP_ERR_INVALID_SIZE;
            goto exit;
        }

        if (desc->enum_count > 0U) {
            if (!config_manager_json_append(buffer,
                                            buffer_size,
                                            &offset,
                                            ",\"enum\":[")) {
                result = ESP_ERR_INVALID_SIZE;
                goto exit;
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
                    goto exit;
                }
            }
            if (!config_manager_json_append(buffer, buffer_size, &offset, "]")) {
                result = ESP_ERR_INVALID_SIZE;
                goto exit;
            }
        }

        if (!config_manager_json_append(buffer, buffer_size, &offset, "}")) {
            result = ESP_ERR_INVALID_SIZE;
            goto exit;
        }
    }

    if (!config_manager_json_append(buffer, buffer_size, &offset, "]}")) {
        result = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

exit:
    config_manager_unlock_mutex();
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

    esp_err_t lock_err = config_manager_lock_mutex(CONFIG_MANAGER_LOCK_TIMEOUT);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    esp_err_t result = ESP_OK;
    size_t index = 0;
    if (!config_manager_find_register(key, &index)) {
        ESP_LOGW(TAG, "Unknown register key %s", key);
        result = ESP_ERR_NOT_FOUND;
        goto exit;
    }

    const config_manager_register_descriptor_t *desc = &s_register_descriptors[index];
    uint16_t raw_value = 0;
    result = config_manager_convert_user_to_raw(desc, requested_value, &raw_value, NULL);
    if (result != ESP_OK) {
        goto exit;
    }

    uint16_t readback_raw = raw_value;
    result = uart_bms_write_register(desc->address,
                                     raw_value,
                                     &readback_raw,
                                     UART_BMS_RESPONSE_TIMEOUT_MS);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to write register %s (0x%04X): %s",
                 desc->key,
                 (unsigned)desc->address,
                 esp_err_to_name(result));
        goto exit;
    }

    s_register_raw_values[index] = readback_raw;
    config_manager_publish_register_change(desc, readback_raw);
#ifdef ESP_PLATFORM
    {
        esp_err_t persist_err = config_manager_store_register_raw(desc->address, readback_raw);
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to persist register 0x%04X: %s",
                     (unsigned)desc->address,
                     esp_err_to_name(persist_err));
        }
    }
#endif
    result = config_manager_build_config_snapshot();

exit:
    config_manager_unlock_mutex();
    return result;
}
