#pragma once

/**
 * @file can_victron.h
 * @brief Victron CAN bus driver interface
 *
 * Low-level CAN bus driver for ESP32 TWAI peripheral configured for
 * Victron Energy protocol. Handles driver lifecycle, keepalive messages,
 * and frame transmission with event bus integration.
 *
 * @section can_victron_thread_safety Thread Safety
 *
 * The CAN Victron module uses multiple mutexes for thread safety:
 * - s_twai_mutex: Protects TWAI hardware access
 * - s_driver_state_mutex: Protects driver start/stop state flag
 *
 * **Protected Resources**:
 * - s_driver_started - Boolean flag indicating TWAI driver state
 * - TWAI hardware registers (ESP32 CAN peripheral)
 * - Keepalive timing state variables
 *
 * **Thread-Safe Functions** (all public functions):
 * - can_victron_init() - Initializes mutexes, starts driver, creates task
 * - can_victron_publish_frame() - Thread-safe frame transmission
 * - can_victron_set_event_publisher() - Sets event callback (init only)
 * - Internal: can_victron_is_driver_started() - Mutex-protected state read
 *
 * **Concurrency Pattern**:
 * - Main thread: Calls init/deinit
 * - CAN task thread: Handles RX messages and keepalive
 * - Publisher threads: Call publish_frame() to send CAN frames
 *
 * **Driver State Management**:
 * The s_driver_started flag is critical for preventing operations on
 * uninitialized hardware. All checks must use mutex protection via
 * can_victron_is_driver_started() helper function.
 *
 * **Synchronization Approach**:
 * - 100ms timeout on mutex operations
 * - Helper function consolidates driver state checks
 * - All 11 accesses to s_driver_started are mutex-protected
 *
 * @warning Do NOT directly check s_driver_started from outside this module.
 *          The flag is protected and must be accessed via the public API.
 *
 * @section can_victron_usage Usage Example
 * @code
 * // Initialize CAN driver
 * can_victron_init();
 * can_victron_set_event_publisher(my_publisher);
 *
 * // Publish a frame (thread-safe)
 * uint8_t data[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
 * esp_err_t err = can_victron_publish_frame(0x351, data, 8, "CVL/CCL/DCL");
 * @endcode
 */

#include "esp_err.h"

#ifdef ESP_PLATFORM
#include "driver/twai.h"
#else
typedef enum {
    TWAI_STATE_STOPPED = 0,
    TWAI_STATE_RUNNING = 1,
    TWAI_STATE_BUS_OFF = 2,
    TWAI_STATE_RECOVERING = 3,
    TWAI_STATE_BUS_OFF_RECOVERING = 4,
} twai_state_t;
#endif

#include "event_bus.h"

typedef struct {
    bool driver_started;
    bool keepalive_ok;
    uint64_t timestamp_ms;
    uint64_t last_keepalive_tx_ms;
    uint64_t last_keepalive_rx_ms;
    uint32_t keepalive_interval_ms;
    uint32_t keepalive_timeout_ms;
    uint32_t keepalive_retry_ms;
    uint64_t tx_frame_count;
    uint64_t rx_frame_count;
    uint64_t tx_byte_count;
    uint64_t rx_byte_count;
    uint32_t tx_error_counter;
    uint32_t rx_error_counter;
    uint32_t tx_failed_count;
    uint32_t rx_missed_count;
    uint32_t arbitration_lost_count;
    uint32_t bus_error_count;
    uint32_t bus_off_count;
    twai_state_t bus_state;
    float bus_occupancy_pct;
    uint32_t occupancy_window_ms;
} can_victron_status_t;

void can_victron_init(void);
void can_victron_deinit(void);
void can_victron_set_event_publisher(event_bus_publish_fn_t publisher);
esp_err_t can_victron_publish_frame(uint32_t can_id,
                                    const uint8_t *data,
                                    size_t length,
                                    const char *description);
esp_err_t can_victron_get_status(can_victron_status_t *status);
