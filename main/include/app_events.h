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
    /** Aggregated TinyBMS metrics ready for MQTT publishing. */
    APP_EVENT_ID_MQTT_METRICS = 0x1004,
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
    /** Binary CAN frame prepared by the CAN publisher. */
    APP_EVENT_ID_CAN_FRAME_READY = 0x1202,
    /** Wi-Fi station interface has started. */
    APP_EVENT_ID_WIFI_STA_START = 0x1300,
    /** Wi-Fi station connected to the configured access point. */
    APP_EVENT_ID_WIFI_STA_CONNECTED = 0x1301,
    /** Wi-Fi station disconnected from the access point. */
    APP_EVENT_ID_WIFI_STA_DISCONNECTED = 0x1302,
    /** Wi-Fi station obtained an IPv4 address. */
    APP_EVENT_ID_WIFI_STA_GOT_IP = 0x1303,
    /** Wi-Fi station lost its IPv4 address. */
    APP_EVENT_ID_WIFI_STA_LOST_IP = 0x1304,
    /** Wi-Fi fallback access point started. */
    APP_EVENT_ID_WIFI_AP_STARTED = 0x1310,
    /** Wi-Fi fallback access point stopped. */
    APP_EVENT_ID_WIFI_AP_STOPPED = 0x1311,
    /** A client associated with the Wi-Fi access point. */
    APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED = 0x1312,
    /** A client disconnected from the Wi-Fi access point. */
    APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED = 0x1313,
    /** History flash storage successfully mounted and available. */
    APP_EVENT_ID_STORAGE_HISTORY_READY = 0x1400,
    /** History flash storage missing or unavailable. */
    APP_EVENT_ID_STORAGE_HISTORY_UNAVAILABLE = 0x1401,
    /** Alert was triggered by the alert manager. */
    APP_EVENT_ID_ALERT_TRIGGERED = 0x1500,
} app_event_id_t;

#ifdef __cplusplus
}
#endif

