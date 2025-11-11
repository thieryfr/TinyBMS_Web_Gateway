#include "config_manager.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

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
#include "esp_system.h"
#endif

#if CONFIG_TINYBMS_WIFI_ENABLE
void wifi_start_sta_mode(void) __attribute__((weak));
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
#define CONFIG_MANAGER_MQTT_TLS_CLIENT_KEY   "mqtt_tls_cli"
#define CONFIG_MANAGER_MQTT_TLS_CA_KEY       "mqtt_tls_ca"
#define CONFIG_MANAGER_MQTT_TLS_VERIFY_KEY   "mqtt_tls_vrf"
#define CONFIG_MANAGER_MQTT_TOPIC_STATUS_KEY "mqtt_t_stat"
#define CONFIG_MANAGER_MQTT_TOPIC_MET_KEY    "mqtt_t_met"
#define CONFIG_MANAGER_MQTT_TOPIC_CFG_KEY    "mqtt_t_cfg"
#define CONFIG_MANAGER_MQTT_TOPIC_RAW_KEY    "mqtt_t_crw"
#define CONFIG_MANAGER_MQTT_TOPIC_DEC_KEY    "mqtt_t_cdc"
#define CONFIG_MANAGER_MQTT_TOPIC_RDY_KEY    "mqtt_t_crd"
#define CONFIG_MANAGER_WIFI_AP_SECRET_KEY    "wifi_ap_secret"

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
#define CONFIG_MANAGER_MQTT_DEFAULT_RETAIN          (CONFIG_TINYBMS_MQTT_RETAIN_STATUS != 0)
#define CONFIG_MANAGER_MQTT_DEFAULT_CLIENT_CERT     ""
#define CONFIG_MANAGER_MQTT_DEFAULT_CA_CERT         ""
#define CONFIG_MANAGER_MQTT_DEFAULT_VERIFY_HOSTNAME true

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

#define CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH 8U
#define CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH    16U

#ifndef CONFIG_TINYBMS_WIFI_ENABLE
#define CONFIG_TINYBMS_WIFI_ENABLE 1
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

static void config_manager_generate_random_bytes(uint8_t *buffer, size_t length)
{
    if (buffer == NULL || length == 0) {
        return;
    }

#ifdef ESP_PLATFORM
    esp_fill_random(buffer, length);
#else
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        unsigned int seed = (unsigned int)time(NULL);
        seed ^= (unsigned int)clock();
        srand(seed);
    }
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

static void config_manager_generate_ap_secret(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const size_t alphabet_len = sizeof(alphabet) - 1U;
    if (alphabet_len == 0U) {
        out[0] = '\0';
        return;
    }

    uint8_t random_bytes[CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH];
    memset(random_bytes, 0, sizeof(random_bytes));
    config_manager_generate_random_bytes(random_bytes, sizeof(random_bytes));

    size_t required = CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH + 1U;
    if (out_size < required) {
        required = out_size;
    }

    size_t limit = required - 1U;
    for (size_t i = 0; i < limit; ++i) {
        out[i] = alphabet[random_bytes[i] % alphabet_len];
    }
    out[limit] = '\0';
}

static void config_manager_store_ap_secret_to_nvs(const char *secret)
{
#ifdef ESP_PLATFORM
    if (secret == NULL || secret[0] == '\0') {
        return;
    }

    if (config_manager_init_nvs() != ESP_OK) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for AP secret: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(handle, CONFIG_MANAGER_WIFI_AP_SECRET_KEY, secret);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist AP secret: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
#else
    (void)secret;
#endif
}

static void config_manager_ensure_ap_secret_loaded(void)
{
    if (s_wifi_ap_secret_loaded) {
        return;
    }

#ifdef ESP_PLATFORM
    if (config_manager_init_nvs() != ESP_OK) {
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        s_wifi_ap_secret_loaded = true;
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for Wi-Fi secret: %s", esp_err_to_name(err));
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        s_wifi_ap_secret_loaded = true;
        return;
    }

    size_t length = sizeof(s_wifi_ap_secret);
    err = nvs_get_str(handle, CONFIG_MANAGER_WIFI_AP_SECRET_KEY, s_wifi_ap_secret, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND ||
        strlen(s_wifi_ap_secret) < CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        if (strlen(s_wifi_ap_secret) >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
            config_manager_store_ap_secret_to_nvs(s_wifi_ap_secret);
        }
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read AP secret from NVS: %s", esp_err_to_name(err));
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
    }

    nvs_close(handle);
#else
    config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
#endif

    s_wifi_ap_secret_loaded = true;
}

static void config_manager_apply_ap_secret_if_needed(config_manager_wifi_settings_t *wifi)
{
    if (wifi == NULL) {
        return;
    }

    size_t password_len = strnlen(wifi->ap.password, sizeof(wifi->ap.password));
    if (password_len >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        return;
    }

    config_manager_ensure_ap_secret_loaded();
    if (strlen(s_wifi_ap_secret) >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        config_manager_copy_string(wifi->ap.password,
                                   sizeof(wifi->ap.password),
                                   s_wifi_ap_secret);
    } else {
        ESP_LOGW(TAG, "No valid AP secret available; fallback AP will remain disabled");
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
    .client_cert_path = CONFIG_MANAGER_MQTT_DEFAULT_CLIENT_CERT,
    .ca_cert_path = CONFIG_MANAGER_MQTT_DEFAULT_CA_CERT,
    .keepalive_seconds = CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE,
    .default_qos = CONFIG_MANAGER_MQTT_DEFAULT_QOS,
    .retain_enabled = CONFIG_MANAGER_MQTT_DEFAULT_RETAIN,
    .verify_hostname = CONFIG_MANAGER_MQTT_DEFAULT_VERIFY_HOSTNAME,
};

static config_manager_mqtt_topics_t s_mqtt_topics = {0};
static bool s_mqtt_topics_loaded = false;

static mqtt_client_config_t s_mqtt_config_snapshot = {0};
static config_manager_mqtt_topics_t s_mqtt_topics_snapshot = {0};
static config_manager_device_settings_t s_device_settings_snapshot = {0};
static config_manager_uart_pins_t s_uart_pins_snapshot = {0};
static config_manager_wifi_settings_t s_wifi_settings_snapshot = {0};
static config_manager_can_settings_t s_can_settings_snapshot = {0};
static char s_device_name_snapshot[CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH] = {0};

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

static char s_wifi_ap_secret[CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH] = {0};
static bool s_wifi_ap_secret_loaded = false;

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

    if (config->verify_hostname != true && config->verify_hostname != false) {
        config->verify_hostname = CONFIG_MANAGER_MQTT_DEFAULT_VERIFY_HOSTNAME;
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

    buffer_size = sizeof(s_mqtt_config.client_cert_path);
    err = nvs_get_str(handle,
                      CONFIG_MANAGER_MQTT_TLS_CLIENT_KEY,
                      s_mqtt_config.client_cert_path,
                      &buffer_size);
    if (err != ESP_OK) {
        config_manager_copy_string(s_mqtt_config.client_cert_path,
                                   sizeof(s_mqtt_config.client_cert_path),
                                   CONFIG_MANAGER_MQTT_DEFAULT_CLIENT_CERT);
    }

    buffer_size = sizeof(s_mqtt_config.ca_cert_path);
    err = nvs_get_str(handle,
                      CONFIG_MANAGER_MQTT_TLS_CA_KEY,
                      s_mqtt_config.ca_cert_path,
                      &buffer_size);
    if (err != ESP_OK) {
        config_manager_copy_string(s_mqtt_config.ca_cert_path,
                                   sizeof(s_mqtt_config.ca_cert_path),
                                   CONFIG_MANAGER_MQTT_DEFAULT_CA_CERT);
    }

    uint8_t verify = CONFIG_MANAGER_MQTT_DEFAULT_VERIFY_HOSTNAME ? 1U : 0U;
    err = nvs_get_u8(handle, CONFIG_MANAGER_MQTT_TLS_VERIFY_KEY, &verify);
    if (err == ESP_OK) {
        s_mqtt_config.verify_hostname = (verify != 0U);
    } else {
        s_mqtt_config.verify_hostname = CONFIG_MANAGER_MQTT_DEFAULT_VERIFY_HOSTNAME;
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
static char s_config_json_full[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
static size_t s_config_length_full = 0;
static char s_config_json_public[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
static size_t s_config_length_public = 0;
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
static const TickType_t CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);

static esp_err_t config_manager_lock(TickType_t timeout)
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

static void config_manager_unlock(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
    }
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

    config_manager_apply_ap_secret_if_needed(&s_wifi_settings);
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

    size_t written = fwrite(s_config_json_full, 1, s_config_length_full, file);
    int flush_result = fflush(file);
    int close_result = fclose(file);
    if (written != s_config_length_full || flush_result != 0 || close_result != 0) {
        ESP_LOGW(TAG,
                 "Failed to write configuration file (written=%zu expected=%zu errno=%d)",
                 written,
                 s_config_length_full,
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
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_TLS_CLIENT_KEY, config->client_cert_path) == ESP_OK);
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_TLS_CA_KEY, config->ca_cert_path) == ESP_OK);
    all_ok &= (nvs_set_u8(handle, CONFIG_MANAGER_MQTT_TLS_VERIFY_KEY, config->verify_hostname ? 1U : 0U) == ESP_OK);

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

static const char *config_manager_select_secret_value(const char *value, bool include_secrets)
{
    if (value == NULL) {
        return "";
    }
    return include_secrets ? value : config_manager_mask_secret(value);
}

static void config_manager_publish_config_snapshot(void)
{
    if (s_event_publisher == NULL || s_config_length_public == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CONFIG_UPDATED,
        .payload = s_config_json_public,
        .payload_size = s_config_length_public + 1,
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

    const char *device_name = config_manager_effective_device_name();
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

static esp_err_t config_manager_build_config_snapshot_locked(void)
{
    config_manager_ensure_topics_loaded();

    esp_err_t err = config_manager_render_config_snapshot_locked(true,
                                                                 s_config_json_full,
                                                                 sizeof(s_config_json_full),
                                                                 &s_config_length_full);
    if (err != ESP_OK) {
        return err;
    }

    err = config_manager_render_config_snapshot_locked(false,
                                                       s_config_json_public,
                                                       sizeof(s_config_json_public),
                                                       &s_config_length_public);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t config_manager_build_config_snapshot(void)
{
    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    esp_err_t result = config_manager_build_config_snapshot_locked();
    config_manager_unlock();
    return result;
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

    if (s_config_length_public == 0) {
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

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning default UART interval due to lock failure");
        return UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    }

    uint32_t interval = s_uart_poll_interval_ms;
    config_manager_unlock();
    return interval;
}

esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    uint32_t clamped = config_manager_clamp_poll_interval(interval_ms);
    if (clamped == s_uart_poll_interval_ms) {
        config_manager_unlock();
        uart_bms_set_poll_interval_ms(clamped);
        return ESP_OK;
    }

    s_uart_poll_interval_ms = clamped;
    uart_bms_set_poll_interval_ms(clamped);

    esp_err_t persist_err = config_manager_store_poll_interval(clamped);
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist UART poll interval: %s", esp_err_to_name(persist_err));
    }

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
        if (persist_err == ESP_OK && s_config_file_loaded) {
            esp_err_t save_err = config_manager_save_config_file();
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to update configuration file: %s", esp_err_to_name(save_err));
            }
        }
    }

    config_manager_unlock();

    if (persist_err != ESP_OK) {
        return persist_err;
    }
    return snapshot_err;
}

const config_manager_uart_pins_t *config_manager_get_uart_pins(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning UART pins without lock");
        return &s_uart_pins;
    }

    s_uart_pins_snapshot = s_uart_pins;
    config_manager_unlock();
    return &s_uart_pins_snapshot;
}

const mqtt_client_config_t *config_manager_get_mqtt_client_config(void)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning MQTT client config without lock");
        return &s_mqtt_config;
    }

    s_mqtt_config_snapshot = s_mqtt_config;
    const mqtt_client_config_t *config = &s_mqtt_config_snapshot;
    config_manager_unlock();
    return config;
}

esp_err_t config_manager_set_mqtt_client_config(const mqtt_client_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    mqtt_client_config_t updated = s_mqtt_config;
    config_manager_copy_string(updated.broker_uri, sizeof(updated.broker_uri), config->broker_uri);
    config_manager_copy_string(updated.username, sizeof(updated.username), config->username);
    config_manager_copy_string(updated.password, sizeof(updated.password), config->password);
    config_manager_copy_string(updated.client_cert_path,
                               sizeof(updated.client_cert_path),
                               config->client_cert_path);
    config_manager_copy_string(updated.ca_cert_path,
                               sizeof(updated.ca_cert_path),
                               config->ca_cert_path);
    updated.keepalive_seconds = (config->keepalive_seconds == 0U)
                                    ? CONFIG_MANAGER_MQTT_DEFAULT_KEEPALIVE
                                    : config->keepalive_seconds;
    updated.default_qos = config->default_qos;
    updated.retain_enabled = config->retain_enabled;
    updated.verify_hostname = config->verify_hostname;

    config_manager_sanitise_mqtt_config(&updated);

    esp_err_t err = config_manager_store_mqtt_config_to_nvs(&updated);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist MQTT configuration: %s", esp_err_to_name(err));
        config_manager_unlock();
        return err;
    }

    s_mqtt_config = updated;

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    } else {
        ESP_LOGW(TAG, "Failed to rebuild configuration snapshot: %s", esp_err_to_name(snapshot_err));
    }
    config_manager_unlock();
    return snapshot_err;
}

const config_manager_mqtt_topics_t *config_manager_get_mqtt_topics(void)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning MQTT topics without lock");
        return &s_mqtt_topics;
    }

    s_mqtt_topics_snapshot = s_mqtt_topics;
    const config_manager_mqtt_topics_t *topics = &s_mqtt_topics_snapshot;
    config_manager_unlock();
    return topics;
}

esp_err_t config_manager_set_mqtt_topics(const config_manager_mqtt_topics_t *topics)
{
    if (topics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    config_manager_mqtt_topics_t updated = s_mqtt_topics;
    config_manager_copy_topics(&updated, topics);
    config_manager_sanitise_mqtt_topics(&updated);

    esp_err_t err = config_manager_store_mqtt_topics_to_nvs(&updated);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist MQTT topics: %s", esp_err_to_name(err));
        config_manager_unlock();
        return err;
    }

    s_mqtt_topics = updated;

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
    } else {
        ESP_LOGW(TAG, "Failed to rebuild configuration snapshot after topic update: %s", esp_err_to_name(snapshot_err));
    }
    config_manager_unlock();
    return snapshot_err;
}

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

const config_manager_device_settings_t *config_manager_get_device_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning device settings without lock");
        return &s_device_settings;
    }

    s_device_settings_snapshot = s_device_settings;
    config_manager_unlock();
    return &s_device_settings_snapshot;
}

const char *config_manager_get_device_name(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning device name without lock");
        return config_manager_effective_device_name();
    }

    const char *effective = config_manager_effective_device_name();
    config_manager_copy_string(s_device_name_snapshot,
                               sizeof(s_device_name_snapshot),
                               effective);
    config_manager_unlock();
    return s_device_name_snapshot;
}

const config_manager_wifi_settings_t *config_manager_get_wifi_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning WiFi settings without lock");
        return &s_wifi_settings;
    }

    s_wifi_settings_snapshot = s_wifi_settings;
    config_manager_unlock();
    return &s_wifi_settings_snapshot;
}

const config_manager_can_settings_t *config_manager_get_can_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning CAN settings without lock");
        return &s_can_settings;
    }

    s_can_settings_snapshot = s_can_settings;
    config_manager_unlock();
    return &s_can_settings_snapshot;
}

const char *config_manager_mask_secret(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "";
    }
    return CONFIG_MANAGER_SECRET_MASK;
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
    esp_err_t persist_err = config_manager_store_register_raw(desc->address, readback_raw);
#endif

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

void config_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing config manager...");

#ifdef ESP_PLATFORM
    // Unmount SPIFFS if mounted
    if (s_spiffs_mounted) {
        esp_err_t err = esp_vfs_spiffs_unregister(NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unmount SPIFFS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "SPIFFS unmounted");
        }
        s_spiffs_mounted = false;
    }
#endif

    // Destroy mutex
    if (s_config_mutex != NULL) {
        vSemaphoreDelete(s_config_mutex);
        s_config_mutex = NULL;
    }

    // Reset state
    s_event_publisher = NULL;
    s_config_length_full = 0;
    s_config_length_public = 0;
    s_registers_initialised = false;
    s_settings_loaded = false;
    s_nvs_initialised = false;
    s_mqtt_topics_loaded = false;
    s_config_file_loaded = false;
    s_next_register_event = 0;
    s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    memset(s_config_json_full, 0, sizeof(s_config_json_full));
    memset(s_config_json_public, 0, sizeof(s_config_json_public));
    memset(s_register_raw_values, 0, sizeof(s_register_raw_values));
    memset(s_register_events, 0, sizeof(s_register_events));
    memset(&s_mqtt_config, 0, sizeof(s_mqtt_config));
    memset(&s_mqtt_topics, 0, sizeof(s_mqtt_topics));
    memset(&s_device_settings, 0, sizeof(s_device_settings));
    memset(&s_uart_pins, 0, sizeof(s_uart_pins));
    memset(&s_wifi_settings, 0, sizeof(s_wifi_settings));
    memset(&s_can_settings, 0, sizeof(s_can_settings));
    memset(s_wifi_ap_secret, 0, sizeof(s_wifi_ap_secret));
    s_wifi_ap_secret_loaded = false;

    ESP_LOGI(TAG, "Config manager deinitialized");
}
