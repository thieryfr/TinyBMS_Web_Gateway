#pragma once

/**
 * @file can_publisher.h
 * @brief CAN frame publisher and scheduler module
 *
 * Manages periodic CAN frame generation from BMS data and dispatches
 * frames to the CAN bus driver. Supports both immediate dispatch and
 * periodic scheduling modes.
 *
 * @section can_publisher_thread_safety Thread Safety
 *
 * The CAN publisher uses multiple mutexes for different resources:
 * - s_buffer_mutex: Protects frame buffer (s_frame_buffer)
 * - s_event_mutex: Protects event frame slots (s_event_frames)
 *
 * **Protected Resources**:
 * - s_frame_buffer - Buffered frames waiting for publication
 * - s_event_frames[] - Pre-allocated event buffers (8 slots)
 * - s_event_frame_index - Current event slot index
 *
 * **Thread-Safe Functions** (all public functions are thread-safe):
 * - can_publisher_init() - Initializes mutexes and registers BMS listener
 * - can_publisher_deinit() - Cleans up resources in safe order
 * - can_publisher_publish_frame() - Thread-safe frame publication
 * - Internal: can_publisher_on_bms_update() - Encodes frames (mutex-protected)
 *
 * **Concurrency Pattern**:
 * - BMS callback thread: Generates frames from UART data
 * - Publisher task thread: Schedules periodic frame publication
 * - Event bus subscribers: Receive frame ready notifications
 *
 * **Synchronization Approach**:
 * - Uses consistent mutex strategy (no mixed spinlock/mutex)
 * - 100ms timeout on all mutex operations
 * - Safe shutdown: unregister BMS listener → wait → delete task
 *
 * @note The module was refactored to use mutex everywhere (previously
 *       mixed portMUX_TYPE spinlock with SemaphoreHandle_t).
 *
 * @section can_publisher_usage Usage Example
 * @code
 * // Initialize with event publisher and frame publisher callbacks
 * can_publisher_init(event_publisher, frame_publisher);
 *
 * // Module automatically handles frame generation from BMS data
 * // Cleanup
 * can_publisher_deinit();
 * @endcode
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "event_bus.h"
#include "uart_bms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of buffered CAN frames retained for event publication. */
#define CAN_PUBLISHER_MAX_BUFFER_SLOTS 8U

/**
 * @brief Lightweight representation of a CAN frame scheduled for publication.
 */
typedef struct {
    uint32_t id;          /**< 29-bit or 11-bit CAN identifier. */
    uint8_t dlc;          /**< Data length code, limited to eight bytes. */
    uint8_t data[8];      /**< Frame payload encoded according to the Victron spec. */
    uint64_t timestamp_ms;/**< Timestamp associated with the originating TinyBMS sample. */
} can_publisher_frame_t;

/**
 * @brief Signature of conversion functions producing CAN payloads from TinyBMS telemetry.
 */
typedef bool (*can_publisher_fill_frame_fn_t)(const uart_bms_live_data_t *bms_data,
                                               can_publisher_frame_t *out_frame);

/**
 * @brief CAN channel description used by the publisher registry.
 */
typedef struct {
    uint16_t pgn;                            /**< Victron PGN identifier (11-bit). */
    uint32_t can_id;                         /**< CAN identifier associated with the channel. */
    uint8_t dlc;                             /**< Expected payload size for the channel. */
    can_publisher_fill_frame_fn_t fill_fn;   /**< Encoder translating TinyBMS fields. */
    const char *description;                 /**< Human readable description of the channel. */
    uint32_t period_ms;                      /**< Dispatch period for the channel (0 = inherit global). */
} can_publisher_channel_t;

/**
 * @brief Function signature for low level CAN transmit hooks.
 */
typedef esp_err_t (*can_publisher_frame_publish_fn_t)(uint32_t can_id,
                                                      const uint8_t *data,
                                                      size_t length,
                                                      const char *description);

/**
 * @brief Shared buffer storing the most recent frames prepared for each channel.
 */
typedef struct {
    can_publisher_frame_t slots[CAN_PUBLISHER_MAX_BUFFER_SLOTS]; /**< Storage for prepared frames. */
    bool slot_valid[CAN_PUBLISHER_MAX_BUFFER_SLOTS];             /**< Whether the slot contains data. */
    size_t capacity;                                            /**< Number of usable slots. */
} can_publisher_buffer_t;

/**
 * @brief Registry binding the static channel catalogue with the shared frame buffer.
 */
typedef struct {
    const can_publisher_channel_t *channels; /**< List of CAN channels handled by the module. */
    size_t channel_count;                    /**< Number of active entries in \p channels. */
    can_publisher_buffer_t *buffer;          /**< Pointer to the shared circular buffer. */
} can_publisher_registry_t;

void can_publisher_set_event_publisher(event_bus_publish_fn_t publisher);
void can_publisher_init(event_bus_publish_fn_t publisher,
                        can_publisher_frame_publish_fn_t frame_publisher);
void can_publisher_deinit(void);
void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context);

#ifdef __cplusplus
}
#endif

