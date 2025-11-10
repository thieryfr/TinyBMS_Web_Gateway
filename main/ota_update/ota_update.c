#include "ota_update.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifndef OTA_UPDATE_LOCK_TIMEOUT_MS
#define OTA_UPDATE_LOCK_TIMEOUT_MS 5000U
#endif

#ifndef OTA_UPDATE_MIN_IMAGE_SIZE
#define OTA_UPDATE_MIN_IMAGE_SIZE (32 * 1024U)
#endif

struct ota_update_session {
    const esp_partition_t *partition;
    esp_ota_handle_t handle;
    size_t bytes_written;
    uint32_t crc32;
    size_t expected_request_size;
    bool active;
};

static const char *TAG = "ota_update";

static SemaphoreHandle_t s_ota_mutex = NULL;

static SemaphoreHandle_t ota_update_get_mutex(void)
{
    if (s_ota_mutex == NULL) {
        s_ota_mutex = xSemaphoreCreateMutex();
    }
    return s_ota_mutex;
}

static uint32_t ota_update_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = crc ^ 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

esp_err_t ota_update_begin(ota_update_session_t **out_session, size_t expected_request_size)
{
    if (out_session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = ota_update_get_mutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Unable to allocate OTA mutex");
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(OTA_UPDATE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout acquiring OTA mutex");
        return ESP_ERR_TIMEOUT;
    }

    ota_update_session_t *session = calloc(1, sizeof(*session));
    if (session == NULL) {
        xSemaphoreGive(mutex);
        return ESP_ERR_NO_MEM;
    }

    session->partition = esp_ota_get_next_update_partition(NULL);
    if (session->partition == NULL) {
        free(session);
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(session->partition, OTA_SIZE_UNKNOWN, &session->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA on %s: %s", session->partition->label, esp_err_to_name(err));
        free(session);
        xSemaphoreGive(mutex);
        return err;
    }

    session->bytes_written = 0U;
    session->crc32 = 0U;
    session->expected_request_size = expected_request_size;
    session->active = true;

    ESP_LOGI(TAG, "OTA session opened on partition '%s' (size=%" PRIu32 ")", session->partition->label,
             session->partition->size);
    *out_session = session;
    return ESP_OK;
}

esp_err_t ota_update_write(ota_update_session_t *session, const void *data, size_t length)
{
    if (session == NULL || data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!session->active) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_write(session->handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed after %zu bytes: %s", session->bytes_written, esp_err_to_name(err));
        return err;
    }

    session->crc32 = ota_update_crc32_update(session->crc32, data, length);
    session->bytes_written += length;
    return ESP_OK;
}

static void ota_update_release(ota_update_session_t *session)
{
    SemaphoreHandle_t mutex = ota_update_get_mutex();
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
    if (session != NULL) {
        free(session);
    }
}

static void ota_update_populate_result(const ota_update_session_t *session,
                                       const esp_app_desc_t *new_desc,
                                       const esp_app_desc_t *current_desc,
                                       ota_update_result_t *out_result,
                                       bool version_changed)
{
    if (out_result == NULL) {
        return;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->bytes_written = session->bytes_written;
    out_result->crc32 = session->crc32;
    out_result->version_changed = version_changed;
    out_result->reboot_required = version_changed;
    strncpy(out_result->partition_label, session->partition->label, sizeof(out_result->partition_label) - 1U);

    if (new_desc != NULL) {
        strncpy(out_result->new_version, new_desc->version, sizeof(out_result->new_version) - 1U);
    }

    (void)current_desc;
}

esp_err_t ota_update_finalize(ota_update_session_t *session, ota_update_result_t *out_result)
{
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!session->active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (session->bytes_written < OTA_UPDATE_MIN_IMAGE_SIZE) {
        ESP_LOGE(TAG, "OTA payload too small: %zu bytes", session->bytes_written);
        esp_ota_abort(session->handle);
        session->active = false;
        ota_update_release(session);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_end(session->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        session->active = false;
        ota_update_release(session);
        return err;
    }

    esp_app_desc_t new_desc = {0};
    err = esp_ota_get_partition_description(session->partition, &new_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read OTA descriptor: %s", esp_err_to_name(err));
        session->active = false;
        ota_update_release(session);
        return err;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    esp_app_desc_t running_desc = {0};
    if (running_partition != NULL) {
        esp_err_t desc_err = esp_ota_get_partition_description(running_partition, &running_desc);
        if (desc_err != ESP_OK) {
            ESP_LOGW(TAG, "Unable to read running partition descriptor: %s", esp_err_to_name(desc_err));
        }
    }

    bool version_changed = true;
    if (running_desc.version[0] != '\0') {
        version_changed = (strncmp(new_desc.version, running_desc.version, sizeof(new_desc.version)) != 0);
    }

    if (version_changed) {
        err = esp_ota_set_boot_partition(session->partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to select OTA partition %s: %s", session->partition->label, esp_err_to_name(err));
            session->active = false;
            ota_update_release(session);
            return err;
        }
    } else {
        ESP_LOGI(TAG, "OTA image matches running version (%s), keeping current boot partition", new_desc.version);
    }

    ota_update_populate_result(session, &new_desc, &running_desc, out_result, version_changed);

    ESP_LOGI(TAG, "OTA update complete: %zu bytes written, crc32=0x%08" PRIX32 ", version=%s", session->bytes_written,
             session->crc32, new_desc.version);

    session->active = false;
    ota_update_release(session);
    return ESP_OK;
}

void ota_update_abort(ota_update_session_t *session)
{
    if (session == NULL) {
        return;
    }

    if (session->active) {
        esp_ota_abort(session->handle);
        session->active = false;
    }

    ota_update_release(session);
}

