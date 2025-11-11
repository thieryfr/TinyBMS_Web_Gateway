/**
 * @file config_manager_mqtt.c
 * @brief MQTT configuration management
 *
 * This module manages MQTT broker settings, topic configuration, and
 * persistence of MQTT-related settings to NVS storage.
 */

#include "config_manager.h"

#include <string.h>
#include <ctype.h>

#include "esp_log.h"

#include "app_config.h"
#include "mqtt_topics.h"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#endif

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

#define CONFIG_MANAGER_NAMESPACE "gateway_cfg"

static const char *TAG = "config_manager_mqtt";

// Forward declarations for helper functions (shared with other modules)
extern void config_manager_copy_string(char *dest, size_t dest_size, const char *src);
extern esp_err_t config_manager_init_nvs(void);
extern const char *config_manager_effective_device_name(void);

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

static const char *config_manager_effective_device_name(void)
{
    // This function is defined in network module, but we need it here
    // It will be properly linked when all modules are compiled together
    extern const char *config_manager_effective_device_name_impl(void);
    return config_manager_effective_device_name_impl();
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
#endif

// Public API functions
extern esp_err_t config_manager_lock(TickType_t timeout);
extern void config_manager_unlock(void);
extern void config_manager_ensure_initialised(void);
extern esp_err_t config_manager_build_config_snapshot_locked(void);
extern void config_manager_publish_config_snapshot(void);

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
