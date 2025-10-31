#pragma once

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
    uint32_t can_id;                         /**< CAN identifier associated with the channel. */
    uint8_t dlc;                             /**< Expected payload size for the channel. */
    can_publisher_fill_frame_fn_t fill_fn;   /**< Encoder translating TinyBMS fields. */
    const char *description;                 /**< Human readable description of the channel. */
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

