/**
 * @file https_config.h
 * @brief HTTPS Configuration for TinyBMS Gateway Web Server
 *
 * This file provides configuration for HTTPS/TLS support.
 *
 * SECURITY NOTE:
 * The default self-signed certificates provided here are for DEVELOPMENT ONLY.
 * For PRODUCTION use, you MUST generate your own certificates.
 *
 * To generate your own certificates:
 *
 * 1. Generate private key:
 *    openssl genrsa -out server_key.pem 2048
 *
 * 2. Generate self-signed certificate (valid 10 years):
 *    openssl req -new -x509 -key server_key.pem -out server_cert.pem -days 3650
 *
 * 3. Convert to C arrays:
 *    xxd -i server_cert.pem > server_cert.h
 *    xxd -i server_key.pem > server_key.h
 *
 * 4. Replace the arrays below with your generated certificates
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration flags
#ifndef CONFIG_TINYBMS_WEB_HTTPS_ENABLED
#define CONFIG_TINYBMS_WEB_HTTPS_ENABLED 0  // Default: HTTP only (compatible)
#endif

#ifndef CONFIG_TINYBMS_WEB_HTTPS_PORT
#define CONFIG_TINYBMS_WEB_HTTPS_PORT 443
#endif

#ifndef CONFIG_TINYBMS_WEB_HTTP_PORT
#define CONFIG_TINYBMS_WEB_HTTP_PORT 80
#endif

// Enable HTTP redirect to HTTPS (only if HTTPS is enabled)
#ifndef CONFIG_TINYBMS_WEB_REDIRECT_HTTP_TO_HTTPS
#define CONFIG_TINYBMS_WEB_REDIRECT_HTTP_TO_HTTPS 1
#endif

/**
 * DEFAULT SELF-SIGNED CERTIFICATE (DEVELOPMENT ONLY)
 *
 * ⚠️ WARNING: DO NOT USE IN PRODUCTION ⚠️
 *
 * This is a default self-signed certificate valid for 10 years.
 * Subject: CN=TinyBMS-GW, O=TinyBMS, C=FR
 * Valid from: 2025-01-01 to 2035-01-01
 *
 * For production:
 * 1. Generate your own certificate (see instructions above)
 * 2. Update these arrays with your certificate data
 * 3. Or disable embedded certificates and load from SPIFFS/NVS
 */

#if CONFIG_TINYBMS_WEB_HTTPS_ENABLED

// Default self-signed certificate (PEM format)
// Generated with: openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 3650
// Subject: CN=tinybms-gw.local, O=TinyBMS Gateway, C=XX
extern const unsigned char server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const unsigned char server_cert_pem_end[] asm("_binary_server_cert_pem_end");

extern const unsigned char server_key_pem_start[] asm("_binary_server_key_pem_start");
extern const unsigned char server_key_pem_end[] asm("_binary_server_key_pem_end");

/**
 * @brief Get server certificate PEM data
 * @param out_length Pointer to store certificate length
 * @return Pointer to certificate data
 */
const char* https_config_get_server_cert(size_t *out_length);

/**
 * @brief Get server private key PEM data
 * @param out_length Pointer to store key length
 * @return Pointer to key data
 */
const char* https_config_get_server_key(size_t *out_length);

/**
 * @brief Check if HTTPS is enabled
 * @return true if HTTPS is enabled, false otherwise
 */
bool https_config_is_enabled(void);

/**
 * @brief Get HTTPS port
 * @return HTTPS port number
 */
uint16_t https_config_get_port(void);

/**
 * @brief Check if HTTP to HTTPS redirect is enabled
 * @return true if redirect is enabled
 */
bool https_config_is_redirect_enabled(void);

#endif // CONFIG_TINYBMS_WEB_HTTPS_ENABLED

#ifdef __cplusplus
}
#endif
