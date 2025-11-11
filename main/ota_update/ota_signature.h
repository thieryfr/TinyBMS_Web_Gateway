/**
 * @file ota_signature.h
 * @brief OTA Firmware Signature Verification
 *
 * This module provides RSA signature verification for OTA firmware updates.
 *
 * SECURITY OVERVIEW:
 * =================
 * 1. Firmware is signed offline with private key (RSA-2048 or RSA-4096)
 * 2. Gateway verifies signature with embedded public key before flashing
 * 3. Invalid signatures are rejected - prevents malicious firmware
 *
 * WORKFLOW:
 * =========
 * Developer Side (Build Server):
 * 1. Build firmware binary
 * 2. Generate SHA256 hash of firmware
 * 3. Sign hash with RSA private key → signature file
 * 4. Upload firmware + signature to gateway
 *
 * Gateway Side (This Code):
 * 1. Receive firmware + signature
 * 2. Compute SHA256 hash of received firmware
 * 3. Verify signature using embedded public key
 * 4. If valid: flash firmware; else: reject
 *
 * KEY GENERATION:
 * ==============
 * Generate RSA key pair (one time, keep private key SECRET):
 *
 *   openssl genrsa -out ota_private_key.pem 2048
 *   openssl rsa -in ota_private_key.pem -pubout -out ota_public_key.pem
 *
 * SIGNING FIRMWARE:
 * =================
 * Sign firmware binary before upload:
 *
 *   # Generate signature
 *   openssl dgst -sha256 -sign ota_private_key.pem \
 *     -out firmware.sig firmware.bin
 *
 *   # Or use our helper script:
 *   ./scripts/sign_firmware.sh firmware.bin ota_private_key.pem
 *
 * EMBEDDING PUBLIC KEY:
 * ====================
 * The public key is embedded in firmware at build time.
 * Place ota_public_key.pem in main/ota_update/keys/ and rebuild.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration
#ifndef CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED
#define CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED 0  // Default: disabled (compatible)
#endif

#ifndef CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE
#define CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE 2048  // RSA key size (2048 or 4096)
#endif

// Signature sizes
#define OTA_SIGNATURE_SIZE_2048 256  // bytes (2048 bits)
#define OTA_SIGNATURE_SIZE_4096 512  // bytes (4096 bits)

#if CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE == 4096
#define OTA_SIGNATURE_SIZE OTA_SIGNATURE_SIZE_4096
#else
#define OTA_SIGNATURE_SIZE OTA_SIGNATURE_SIZE_2048
#endif

/**
 * @brief Initialize OTA signature verification
 *
 * Loads and validates the embedded public key.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if public key not embedded
 * @return ESP_ERR_INVALID_ARG if public key invalid
 */
esp_err_t ota_signature_init(void);

/**
 * @brief Verify firmware signature
 *
 * Verifies that the firmware binary was signed with the private key
 * corresponding to the embedded public key.
 *
 * @param firmware_data Pointer to firmware binary data
 * @param firmware_size Size of firmware in bytes
 * @param signature Pointer to RSA signature (OTA_SIGNATURE_SIZE bytes)
 * @param signature_size Size of signature in bytes
 *
 * @return ESP_OK if signature is valid
 * @return ESP_ERR_INVALID_ARG if parameters are invalid
 * @return ESP_ERR_INVALID_SIZE if signature size doesn't match expected
 * @return ESP_FAIL if signature verification failed (SECURITY: reject firmware)
 */
esp_err_t ota_signature_verify(const uint8_t *firmware_data,
                               size_t firmware_size,
                               const uint8_t *signature,
                               size_t signature_size);

/**
 * @brief Verify firmware signature from file
 *
 * Convenience function that reads firmware from file and verifies.
 *
 * @param firmware_path Path to firmware binary file
 * @param signature_path Path to signature file
 *
 * @return ESP_OK if signature is valid
 * @return ESP_ERR_NOT_FOUND if files not found
 * @return ESP_FAIL if signature verification failed
 */
esp_err_t ota_signature_verify_file(const char *firmware_path,
                                    const char *signature_path);

/**
 * @brief Get embedded public key (for debugging/verification)
 *
 * Returns pointer to embedded public key PEM data.
 * Useful for verification that correct key is embedded.
 *
 * @param out_length Pointer to store key length
 * @return Pointer to public key PEM data, or NULL if not available
 */
const char* ota_signature_get_public_key(size_t *out_length);

/**
 * @brief Check if signature verification is enabled
 *
 * @return true if OTA signature verification is enabled
 */
bool ota_signature_is_enabled(void);

/**
 * @brief Get signature algorithm info
 *
 * @param out_buffer Buffer to store algorithm info string
 * @param buffer_size Size of buffer
 *
 * @return ESP_OK on success
 */
esp_err_t ota_signature_get_info(char *out_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
