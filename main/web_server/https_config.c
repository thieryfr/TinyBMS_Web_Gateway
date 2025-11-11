/**
 * @file https_config.c
 * @brief HTTPS Configuration Implementation
 */

#include "https_config.h"
#include "esp_log.h"

static const char *TAG = "https_config";

#if CONFIG_TINYBMS_WEB_HTTPS_ENABLED

/**
 * EMBEDDED DEFAULT CERTIFICATES
 *
 * These certificates are embedded in the firmware for convenience.
 * They are self-signed and should be replaced in production.
 *
 * To embed your own certificates:
 * 1. Place server_cert.pem and server_key.pem in main/web_server/certs/
 * 2. Update CMakeLists.txt to embed these files:
 *    target_add_binary_data(${COMPONENT_TARGET} "web_server/certs/server_cert.pem" TEXT)
 *    target_add_binary_data(${COMPONENT_TARGET} "web_server/certs/server_key.pem" TEXT)
 * 3. The _binary_*_start/_end symbols will be automatically created
 */

// These symbols are created by CMake's target_add_binary_data()
// If certificates are not embedded, these will need to be defined as empty strings
#ifndef _binary_server_cert_pem_start
// Fallback: minimal self-signed cert placeholder
// In production, this should be replaced with real embedded certificates
static const char default_cert_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"CERTIFICATE DATA HERE - REPLACE WITH REAL CERTIFICATE\n"
"This is a placeholder. See https_config.h for instructions.\n"
"-----END CERTIFICATE-----\n";

static const char default_key_pem[] =
"-----BEGIN PRIVATE KEY-----\n"
"PRIVATE KEY DATA HERE - REPLACE WITH REAL KEY\n"
"This is a placeholder. See https_config.h for instructions.\n"
"-----END PRIVATE KEY-----\n";

const unsigned char *server_cert_pem_start = (const unsigned char*)default_cert_pem;
const unsigned char *server_cert_pem_end = (const unsigned char*)(default_cert_pem + sizeof(default_cert_pem));
const unsigned char *server_key_pem_start = (const unsigned char*)default_key_pem;
const unsigned char *server_key_pem_end = (const unsigned char*)(default_key_pem + sizeof(default_key_pem));
#endif

const char* https_config_get_server_cert(size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = server_cert_pem_end - server_cert_pem_start;
    }
    return (const char*)server_cert_pem_start;
}

const char* https_config_get_server_key(size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = server_key_pem_end - server_key_pem_start;
    }
    return (const char*)server_key_pem_start;
}

bool https_config_is_enabled(void)
{
    return CONFIG_TINYBMS_WEB_HTTPS_ENABLED;
}

uint16_t https_config_get_port(void)
{
    return CONFIG_TINYBMS_WEB_HTTPS_PORT;
}

bool https_config_is_redirect_enabled(void)
{
    return CONFIG_TINYBMS_WEB_REDIRECT_HTTP_TO_HTTPS;
}

#endif // CONFIG_TINYBMS_WEB_HTTPS_ENABLED
