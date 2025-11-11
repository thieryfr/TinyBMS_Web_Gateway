#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ensure the boot counter value is loaded from persistent storage.
 *
 * This function initialises the NVS subsystem on the ESP platform and reads the
 * persisted boot counter value. On host builds it initialises the internal
 * mock state.
 *
 * @return ESP_OK on success, or an esp_err_t describing the failure.
 */
esp_err_t system_boot_counter_init(void);

/**
 * @brief Increment the persistent boot counter and return the updated value.
 *
 * The function initialises the storage as needed, increments the boot counter,
 * persists it to NVS (on ESP platform) and returns the new value. On host
 * builds the value is updated in the mock state.
 *
 * @param[out] out_value Optional pointer receiving the incremented value.
 * @return ESP_OK on success or an esp_err_t error code.
 */
esp_err_t system_boot_counter_increment_and_get(uint32_t *out_value);

/**
 * @brief Retrieve the last loaded boot counter value.
 *
 * If the boot counter has not been initialised yet the function will attempt to
 * load it from persistent storage. When the value cannot be obtained the
 * function returns 0.
 *
 * @return The current boot counter value (0 when unavailable).
 */
uint32_t system_boot_counter_get(void);

#if !defined(ESP_PLATFORM)
/**
 * @brief Reset the mock boot counter state for host-based unit tests.
 */
void system_boot_counter_mock_reset(void);

/**
 * @brief Set the mock boot counter value for host-based unit tests.
 *
 * The value is marked as initialised and returned by system_boot_counter_get().
 */
void system_boot_counter_mock_set(uint32_t value);
#endif

#ifdef __cplusplus
}
#endif

