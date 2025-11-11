/**
 * @file mqtts_config.c
 * @brief MQTT over TLS (MQTTS) configuration implementation
 */

#include "mqtts_config.h"
#include <string.h>
#include <strings.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#define ESP_LOGI(tag, fmt, ...) (void)tag
#define ESP_LOGW(tag, fmt, ...) (void)tag
#define ESP_LOGE(tag, fmt, ...) (void)tag
#endif

static const char *TAG = "mqtts_config";

// External symbols for embedded certificates (defined in CMakeLists.txt)
// These are only available if certificates are embedded via EMBED_FILES
#if CONFIG_TINYBMS_MQTT_TLS_ENABLED

extern const uint8_t mqtt_ca_cert_pem_start[] asm("_binary_mqtt_ca_cert_pem_start");
extern const uint8_t mqtt_ca_cert_pem_end[] asm("_binary_mqtt_ca_cert_pem_end");

#if CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED
extern const uint8_t mqtt_client_cert_pem_start[] asm("_binary_mqtt_client_cert_pem_start");
extern const uint8_t mqtt_client_cert_pem_end[] asm("_binary_mqtt_client_cert_pem_end");
extern const uint8_t mqtt_client_key_pem_start[] asm("_binary_mqtt_client_key_pem_start");
extern const uint8_t mqtt_client_key_pem_end[] asm("_binary_mqtt_client_key_pem_end");
#endif

#endif  // CONFIG_TINYBMS_MQTT_TLS_ENABLED

const char* mqtts_config_get_ca_cert(size_t *out_length)
{
#if CONFIG_TINYBMS_MQTT_TLS_ENABLED
    if (out_length != NULL) {
        *out_length = mqtt_ca_cert_pem_end - mqtt_ca_cert_pem_start;
    }
    return (const char *)mqtt_ca_cert_pem_start;
#else
    if (out_length != NULL) {
        *out_length = 0;
    }
    return NULL;
#endif
}

const char* mqtts_config_get_client_cert(size_t *out_length)
{
#if CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED
    if (out_length != NULL) {
        *out_length = mqtt_client_cert_pem_end - mqtt_client_cert_pem_start;
    }
    return (const char *)mqtt_client_cert_pem_start;
#else
    if (out_length != NULL) {
        *out_length = 0;
    }
    return NULL;
#endif
}

const char* mqtts_config_get_client_key(size_t *out_length)
{
#if CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED
    if (out_length != NULL) {
        *out_length = mqtt_client_key_pem_end - mqtt_client_key_pem_start;
    }
    return (const char *)mqtt_client_key_pem_start;
#else
    if (out_length != NULL) {
        *out_length = 0;
    }
    return NULL;
#endif
}

bool mqtts_config_is_enabled(void)
{
#if CONFIG_TINYBMS_MQTT_TLS_ENABLED
    return true;
#else
    return false;
#endif
}

bool mqtts_config_verify_server(void)
{
#if CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER
    return true;
#else
    return false;
#endif
}

bool mqtts_config_client_cert_enabled(void)
{
#if CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED
    return true;
#else
    return false;
#endif
}

esp_err_t mqtts_config_validate_uri(const char *uri)
{
    if (uri == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_TINYBMS_MQTT_TLS_ENABLED
    // When MQTTS is enabled, only secure URIs are allowed
    bool is_secure = false;

    if (strncasecmp(uri, "mqtts://", 8) == 0) {
        is_secure = true;
    } else if (strncasecmp(uri, "ssl://", 6) == 0) {
        is_secure = true;
    } else if (strncasecmp(uri, "wss://", 6) == 0) {
        is_secure = true;
    }

    if (!is_secure) {
        ESP_LOGE(TAG, "⚠️  SECURITY VIOLATION: Insecure MQTT URI detected");
        ESP_LOGE(TAG, "⚠️  URI: %s", uri);
        ESP_LOGE(TAG, "⚠️  MQTTS is enabled - only secure URIs allowed");
        ESP_LOGE(TAG, "⚠️  Use mqtts://, ssl://, or wss:// instead");
        ESP_LOGE(TAG, "⚠️  Or disable CONFIG_TINYBMS_MQTT_TLS_ENABLED");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "✓ Secure MQTT URI validated: %s", uri);
#else
    // MQTTS disabled - allow any URI (backward compatibility)
    if (strncasecmp(uri, "mqtt://", 7) == 0 ||
        strncasecmp(uri, "tcp://", 6) == 0 ||
        strncasecmp(uri, "ws://", 5) == 0) {
        ESP_LOGW(TAG, "⚠️  WARNING: Using unencrypted MQTT connection");
        ESP_LOGW(TAG, "⚠️  Enable CONFIG_TINYBMS_MQTT_TLS_ENABLED for production");
    }
#endif

    return ESP_OK;
}
