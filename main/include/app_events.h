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
    /** TinyBMS driver published a live telemetry snapshot. */
    APP_EVENT_ID_BMS_LIVE_DATA = 0x1004,
} app_event_id_t;

#ifdef __cplusplus
}
#endif

