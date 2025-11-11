/**
 * @file ota_signature.c
 * @brief OTA Firmware Signature Verification Implementation
 */

#include "ota_signature.h"
#include "esp_log.h"

#if CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"
#include "mbedtls/error.h"
#include <string.h>

static const char *TAG = "ota_signature";

// Embedded public key (created by CMake if key file exists)
extern const unsigned char ota_public_key_pem_start[] asm("_binary_ota_public_key_pem_start");
extern const unsigned char ota_public_key_pem_end[] asm("_binary_ota_public_key_pem_end");

static mbedtls_pk_context s_pk_ctx;
static bool s_initialized = false;

esp_err_t ota_signature_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing OTA signature verification (RSA-%d)",
             CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE);

    // Check if public key is embedded
    size_t key_len = ota_public_key_pem_end - ota_public_key_pem_start;
    if (key_len == 0) {
        ESP_LOGE(TAG, "No public key embedded in firmware!");
        ESP_LOGE(TAG, "Place ota_public_key.pem in main/ota_update/keys/ and rebuild");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Embedded public key size: %zu bytes", key_len);

    // Initialize mbedtls PK context
    mbedtls_pk_init(&s_pk_ctx);

    // Parse public key
    int ret = mbedtls_pk_parse_public_key(&s_pk_ctx,
                                          ota_public_key_pem_start,
                                          key_len + 1);  // +1 for null terminator
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "Failed to parse public key: %s (0x%04X)", error_buf, -ret);
        mbedtls_pk_free(&s_pk_ctx);
        return ESP_ERR_INVALID_ARG;
    }

    // Verify key type is RSA
    if (!mbedtls_pk_can_do(&s_pk_ctx, MBEDTLS_PK_RSA)) {
        ESP_LOGE(TAG, "Public key is not RSA!");
        mbedtls_pk_free(&s_pk_ctx);
        return ESP_ERR_INVALID_ARG;
    }

    // Verify key size matches configuration
    size_t key_bits = mbedtls_pk_get_bitlen(&s_pk_ctx);
    if (key_bits != CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE) {
        ESP_LOGW(TAG, "Public key size (%zu bits) doesn't match config (%d bits)",
                 key_bits, CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE);
    }

    ESP_LOGI(TAG, "Public key loaded successfully (RSA-%zu)", key_bits);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t ota_signature_verify(const uint8_t *firmware_data,
                               size_t firmware_size,
                               const uint8_t *signature,
                               size_t signature_size)
{
    if (firmware_data == NULL || signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        esp_err_t err = ota_signature_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "Verifying firmware signature (firmware: %zu bytes, sig: %zu bytes)",
             firmware_size, signature_size);

    // Verify signature size
    if (signature_size != OTA_SIGNATURE_SIZE) {
        ESP_LOGE(TAG, "Invalid signature size: expected %d, got %zu",
                 OTA_SIGNATURE_SIZE, signature_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Compute SHA-256 hash of firmware
    ESP_LOGI(TAG, "Computing SHA-256 hash of firmware...");
    uint8_t hash[32];  // SHA-256 produces 32 bytes
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);

    int ret = mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to setup MD context");
        mbedtls_md_free(&md_ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_md_starts(&md_ctx);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start MD");
        mbedtls_md_free(&md_ctx);
        return ESP_FAIL;
    }

    // Hash firmware in chunks to avoid large stack usage
    const size_t chunk_size = 4096;
    size_t offset = 0;
    while (offset < firmware_size) {
        size_t chunk = (firmware_size - offset) < chunk_size ?
                       (firmware_size - offset) : chunk_size;
        ret = mbedtls_md_update(&md_ctx, firmware_data + offset, chunk);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to update MD");
            mbedtls_md_free(&md_ctx);
            return ESP_FAIL;
        }
        offset += chunk;

        // Log progress for large files
        if (firmware_size > 100000 && offset % 100000 == 0) {
            ESP_LOGI(TAG, "Hashing progress: %zu/%zu bytes", offset, firmware_size);
        }
    }

    ret = mbedtls_md_finish(&md_ctx, hash);
    mbedtls_md_free(&md_ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to finish MD");
        return ESP_FAIL;
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, hash, sizeof(hash), ESP_LOG_DEBUG);

    // Verify signature
    ESP_LOGI(TAG, "Verifying RSA signature...");
    ret = mbedtls_pk_verify(&s_pk_ctx,
                           MBEDTLS_MD_SHA256,
                           hash,
                           sizeof(hash),
                           signature,
                           signature_size);

    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        ESP_LOGE(TAG, "⚠️  SIGNATURE VERIFICATION FAILED: %s (0x%04X)", error_buf, -ret);
        ESP_LOGE(TAG, "⚠️  FIRMWARE REJECTED - POTENTIAL SECURITY THREAT");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✓ Signature verification SUCCESSFUL");
    ESP_LOGI(TAG, "✓ Firmware authenticity confirmed");

    return ESP_OK;
}

esp_err_t ota_signature_verify_file(const char *firmware_path,
                                    const char *signature_path)
{
    // TODO: Implement file-based verification
    // This would:
    // 1. Open and read firmware file
    // 2. Open and read signature file
    // 3. Call ota_signature_verify()
    // 4. Close files

    ESP_LOGW(TAG, "File-based verification not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

const char* ota_signature_get_public_key(size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = ota_public_key_pem_end - ota_public_key_pem_start;
    }
    return (const char*)ota_public_key_pem_start;
}

bool ota_signature_is_enabled(void)
{
    return CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED;
}

esp_err_t ota_signature_get_info(char *out_buffer, size_t buffer_size)
{
    if (out_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(out_buffer, buffer_size,
                          "OTA Signature Verification:\n"
                          "  Enabled: %s\n"
                          "  Algorithm: RSA-%d with SHA-256\n"
                          "  Signature Size: %d bytes\n"
                          "  Status: %s\n",
                          CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED ? "Yes" : "No",
                          CONFIG_TINYBMS_OTA_SIGNATURE_KEY_SIZE,
                          OTA_SIGNATURE_SIZE,
                          s_initialized ? "Initialized" : "Not initialized");

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

#else // CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED

// Stub implementations when signature verification is disabled

static const char *TAG = "ota_signature";

esp_err_t ota_signature_init(void)
{
    ESP_LOGW(TAG, "OTA signature verification is DISABLED");
    ESP_LOGW(TAG, "Enable in menuconfig for production security");
    return ESP_OK;
}

esp_err_t ota_signature_verify(const uint8_t *firmware_data,
                               size_t firmware_size,
                               const uint8_t *signature,
                               size_t signature_size)
{
    (void)firmware_data;
    (void)firmware_size;
    (void)signature;
    (void)signature_size;

    ESP_LOGW(TAG, "Signature verification DISABLED - accepting firmware without verification");
    return ESP_OK;  // Always accept when disabled
}

esp_err_t ota_signature_verify_file(const char *firmware_path,
                                    const char *signature_path)
{
    (void)firmware_path;
    (void)signature_path;
    return ESP_ERR_NOT_SUPPORTED;
}

const char* ota_signature_get_public_key(size_t *out_length)
{
    if (out_length != NULL) {
        *out_length = 0;
    }
    return NULL;
}

bool ota_signature_is_enabled(void)
{
    return false;
}

esp_err_t ota_signature_get_info(char *out_buffer, size_t buffer_size)
{
    if (out_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(out_buffer, buffer_size,
                          "OTA Signature Verification: DISABLED\n"
                          "⚠️  WARNING: Firmware updates are NOT authenticated\n"
                          "Enable in menuconfig for production security\n");

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

#endif // CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED
