#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result information returned when an OTA session completes.
 */
typedef struct {
    size_t bytes_written;        /**< Number of firmware bytes streamed during the session. */
    uint32_t crc32;              /**< CRC32 of the streamed firmware (IEEE polynomial). */
    bool version_changed;        /**< True when the new firmware version differs from the running image. */
    bool reboot_required;        /**< True when a reboot is needed to boot into the new image. */
    char partition_label[17];    /**< Label of the OTA partition programmed. */
    char new_version[33];        /**< Firmware version extracted from the OTA image descriptor. */
} ota_update_result_t;

/**
 * @brief Opaque OTA session state managed by the OTA update module.
 */
typedef struct ota_update_session ota_update_session_t;

/**
 * @brief Begin a new OTA session targeting the next update partition.
 *
 * Only one OTA session can be active at a time. The function acquires the
 * internal mutex protecting the OTA pipeline and prepares the target
 * partition for streaming writes.
 *
 * @param[out] out_session Pointer receiving the allocated session handle.
 * @param[in]  expected_request_size Total HTTP payload size (used for diagnostics).
 * @return ESP_OK when the session is ready, or an esp_err_t reason otherwise.
 */
esp_err_t ota_update_begin(ota_update_session_t **out_session, size_t expected_request_size);

/**
 * @brief Stream a new firmware chunk to the active OTA session.
 *
 * @param[in] session Active session returned by ::ota_update_begin.
 * @param[in] data    Pointer to the chunk payload.
 * @param[in] length  Number of bytes contained in @p data.
 * @return ESP_OK on success or an esp_err_t code on failure.
 */
esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t length);

/**
 * @brief Finalise the OTA session and select the new partition when needed.
 *
 * @param[in]  session Active session returned by ::ota_update_begin.
 * @param[out] out_result Optional pointer updated with session statistics.
 * @return ESP_OK when the firmware image passes validation, otherwise an
 *         esp_err_t reason.
 */
esp_err_t ota_update_finalize(ota_update_session_t *session, ota_update_result_t *out_result);

/**
 * @brief Abort the OTA session and release associated resources.
 *
 * Safe to call on both active and inactive sessions. When invoked on an
 * inactive session the function behaves as a no-op.
 *
 * @param[in] session Session returned by ::ota_update_begin.
 */
void ota_update_abort(ota_update_session_t *session);

#ifdef __cplusplus
}
#endif

