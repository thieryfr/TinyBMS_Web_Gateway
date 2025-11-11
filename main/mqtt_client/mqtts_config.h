/**
 * @file mqtts_config.h
 * @brief MQTT over TLS (MQTTS) configuration and certificate management
 *
 * This module provides TLS/SSL support for secure MQTT communication.
 *
 * Security features:
 * - TLS 1.2/1.3 encryption
 * - Server certificate verification
 * - Client certificate authentication (optional)
 * - Certificate pinning support
 *
 * Configuration:
 * - Enable via menuconfig: CONFIG_TINYBMS_MQTT_TLS_ENABLED
 * - Certificates embedded in firmware (see certs/README.md)
 */

#ifndef MQTTS_CONFIG_H
#define MQTTS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable/disable MQTTS (MQTT over TLS)
 *
 * When enabled, MQTT connections MUST use TLS encryption.
 * Unencrypted MQTT connections will be rejected.
 *
 * Default: Disabled (for backward compatibility)
 * Production: MUST be enabled
 */
#ifndef CONFIG_TINYBMS_MQTT_TLS_ENABLED
#define CONFIG_TINYBMS_MQTT_TLS_ENABLED 0  // Default: disabled (opt-in)
#endif

/**
 * @brief Require server certificate verification
 *
 * When enabled, broker certificate MUST be valid and trusted.
 * Self-signed certificates require CA cert to be provided.
 *
 * Default: Enabled (recommended for security)
 */
#ifndef CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER
#define CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER 1  // Default: enabled (secure)
#endif

/**
 * @brief Enable client certificate authentication
 *
 * When enabled, client certificate and key are sent to broker.
 * Broker MUST be configured to accept client certificates.
 *
 * Default: Disabled (server verification only)
 */
#ifndef CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED
#define CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED 0  // Default: disabled
#endif

/**
 * @brief MQTTS default port
 */
#define MQTTS_DEFAULT_PORT 8883

/**
 * @brief MQTT standard port (non-TLS)
 */
#define MQTT_DEFAULT_PORT 1883

/**
 * @brief Get CA certificate for server verification
 *
 * Returns pointer to embedded CA certificate (PEM format).
 *
 * @param[out] out_length Length of certificate in bytes (optional)
 * @return Pointer to CA certificate, or NULL if not available
 */
const char* mqtts_config_get_ca_cert(size_t *out_length);

/**
 * @brief Get client certificate for mutual TLS
 *
 * Returns pointer to embedded client certificate (PEM format).
 *
 * @param[out] out_length Length of certificate in bytes (optional)
 * @return Pointer to client certificate, or NULL if not available
 */
const char* mqtts_config_get_client_cert(size_t *out_length);

/**
 * @brief Get client private key for mutual TLS
 *
 * Returns pointer to embedded client private key (PEM format).
 *
 * @param[out] out_length Length of key in bytes (optional)
 * @return Pointer to client key, or NULL if not available
 */
const char* mqtts_config_get_client_key(size_t *out_length);

/**
 * @brief Check if MQTTS is enabled
 *
 * @return true if MQTTS is enabled, false otherwise
 */
bool mqtts_config_is_enabled(void);

/**
 * @brief Check if server verification is enabled
 *
 * @return true if server cert verification is enabled
 */
bool mqtts_config_verify_server(void);

/**
 * @brief Check if client certificate authentication is enabled
 *
 * @return true if client cert auth is enabled
 */
bool mqtts_config_client_cert_enabled(void);

/**
 * @brief Validate broker URI for MQTTS compliance
 *
 * Checks that broker URI uses secure protocol (mqtts://, ssl://, wss://)
 * when MQTTS is enabled.
 *
 * @param uri Broker URI to validate
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if insecure URI with MQTTS enabled
 */
esp_err_t mqtts_config_validate_uri(const char *uri);

#ifdef __cplusplus
}
#endif

#endif  // MQTTS_CONFIG_H
