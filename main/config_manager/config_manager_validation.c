/**
 * @file config_manager_validation.c
 * @brief Configuration validation and data conversion functions
 *
 * This module contains stateless validation and conversion functions for
 * configuration values, including register value validation, clamping,
 * and user-to-raw value conversions.
 */

#include "config_manager.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "uart_bms.h"

static const char *TAG = "config_manager_validation";

// Include register descriptors
#include "generated_tiny_rw_registers.inc"

/**
 * @brief Clamp UART poll interval to valid range
 *
 * @param interval_ms Requested poll interval in milliseconds
 * @return Clamped interval within valid range
 */
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

/**
 * @brief Find register descriptor by key
 *
 * @param key Register key to search for
 * @param index_out Output parameter for register index (optional)
 * @return true if register found, false otherwise
 */
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

/**
 * @brief Convert raw register value to user-facing value
 *
 * @param desc Register descriptor
 * @param raw_value Raw register value
 * @return Scaled user value
 */
static float config_manager_raw_to_user(const config_manager_register_descriptor_t *desc, uint16_t raw_value)
{
    if (desc == NULL) {
        return 0.0f;
    }
    return (float)raw_value * desc->scale;
}

/**
 * @brief Align raw value to step boundaries and clamp to limits
 *
 * @param desc Register descriptor with step and limit info
 * @param requested_raw Requested raw value (may be unaligned)
 * @param out_raw Output parameter for aligned raw value
 * @return ESP_OK if successful, error otherwise
 */
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

/**
 * @brief Convert user value to raw register value with validation
 *
 * Validates that the value is writable, within range, and converts it
 * to the appropriate raw format (numeric or enum).
 *
 * @param desc Register descriptor
 * @param user_value User-facing value to convert
 * @param out_raw Output parameter for raw register value
 * @param out_aligned_user Output parameter for aligned user value (optional)
 * @return ESP_OK if successful, error otherwise
 */
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
