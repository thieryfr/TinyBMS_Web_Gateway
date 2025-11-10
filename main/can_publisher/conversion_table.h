#pragma once

/**
 * @file conversion_table.h
 * @brief TinyBMS to Victron CAN protocol conversion and energy tracking
 *
 * Provides Victron PGN encoders, energy counter management, and CAN channel
 * registry for the CAN publisher module.
 *
 * @section conversion_table_thread_safety Thread Safety
 *
 * **Energy Counter Protection**:
 * The module uses s_energy_mutex to protect cumulative energy counters:
 * - s_energy_charged_wh - Total energy charged (Wh)
 * - s_energy_discharged_wh - Total energy discharged (Wh)
 * - Related persistence state variables
 *
 * **Thread-Safe Functions** (mutex-protected energy operations):
 * - can_publisher_conversion_ingest_sample() - Integrate incoming TinyBMS sample
 * - can_publisher_conversion_set_energy_state() - Atomic state update
 * - can_publisher_conversion_get_energy_state() - Atomic state read
 * - can_publisher_conversion_persist_energy_state() - NVS write
 * - can_publisher_conversion_restore_energy_state() - NVS read
 * - Internal: update_energy_counters() - Integration calculation
 * - Internal: encode_energy_counters() - CAN frame encoding
 *
 * **PGN Encoder Thread Safety**:
 * Most PGN encoders (23 total) are thread-safe as they only read from
 * the input BMS data parameter. Exception: encode_energy_counters()
 * reads mutex-protected energy state.
 *
 * **Concurrency Pattern**:
 * - BMS callback thread: Calls can_publisher_conversion_ingest_sample() to integrate power
 * - CAN publisher thread: Calls encode_energy_counters() to read for frames
 * - Persistence thread: Periodically saves to NVS
 *
 * **Critical Section**:
 * Energy integration (V × I × Δt) must be atomic to prevent corruption.
 * The mutex ensures power integration and counter reads are serialized.
 *
 * @warning Do NOT directly access energy counter variables outside this module.
 *          Always use the provided API functions which handle mutex protection.
 *
 * @section conversion_table_usage Usage Example
 * @code
 * // Restore energy counters from NVS on startup
 * esp_err_t err = can_publisher_conversion_restore_energy_state();
 *
 * // Read current energy counters (thread-safe)
 * double charged, discharged;
 * can_publisher_conversion_get_energy_state(&charged, &discharged);
 *
 * // Force persistence to NVS (thread-safe)
 * err = can_publisher_conversion_persist_energy_state();
 * @endcode
 *
 * @see conversion_table.md for detailed documentation and refactoring plan
 */

#include <stddef.h>

#include "can_publisher.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const can_publisher_channel_t g_can_publisher_channels[];
extern const size_t g_can_publisher_channel_count;

void can_publisher_conversion_reset_state(void);
void can_publisher_conversion_ingest_sample(const uart_bms_live_data_t *sample);
void can_publisher_conversion_set_energy_state(double charged_wh, double discharged_wh);
void can_publisher_conversion_get_energy_state(double *charged_wh, double *discharged_wh);
esp_err_t can_publisher_conversion_restore_energy_state(void);
esp_err_t can_publisher_conversion_persist_energy_state(void);

#ifdef __cplusplus
}
#endif

