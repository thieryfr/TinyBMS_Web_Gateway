#pragma once

#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application specific event identifiers published on the event bus.
 */
typedef enum {
    /** JSON encoded telemetry samples emitted by the monitoring module. */
    APP_EVENT_ID_TELEMETRY_SAMPLE = 0x1000,
    /** Human readable notification message for the UI event feed. */
    APP_EVENT_ID_UI_NOTIFICATION = 0x1001,
    /** Configuration has been updated through the REST API. */
    APP_EVENT_ID_CONFIG_UPDATED = 0x1002,
    /** Firmware upload was received via the OTA endpoint. */
    APP_EVENT_ID_OTA_UPLOAD_READY = 0x1003,
    /** Decoded TinyBMS live telemetry sample. */
    APP_EVENT_ID_BMS_LIVE_DATA = 0x1100,
    /** Raw TinyBMS UART frame as hexadecimal string. */
    APP_EVENT_ID_UART_FRAME_RAW = 0x1101,
    /** Decoded TinyBMS UART frame content. */
    APP_EVENT_ID_UART_FRAME_DECODED = 0x1102,
    /** Raw CAN frame received on the Victron bus. */
    APP_EVENT_ID_CAN_FRAME_RAW = 0x1200,
    /** Human readable representation of a CAN frame. */
    APP_EVENT_ID_CAN_FRAME_DECODED = 0x1201,
} app_event_id_t;

#ifdef __cplusplus
}
#endif

