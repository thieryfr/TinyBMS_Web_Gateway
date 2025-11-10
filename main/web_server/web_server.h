#pragma once

#include "event_bus.h"

// WebSocket rate limiting and security constants
#define WEB_SERVER_WS_MAX_PAYLOAD_SIZE    (32 * 1024)  // 32KB max payload
#define WEB_SERVER_WS_MAX_MSGS_PER_SEC    10           // Max 10 messages/sec per client
#define WEB_SERVER_WS_RATE_WINDOW_MS      1000         // 1 second rate limiting window

/**
 * @brief Initialise the embedded HTTP server and register REST/WebSocket handlers.
 *
 * Endpoints exposed by the server:
 *   - GET  /api/metrics/runtime
 *   - GET  /api/event-bus/metrics
 *   - GET  /api/system/tasks
 *   - GET  /api/system/modules
 *   - POST /api/system/restart
 *   - GET  /api/status
 *   - GET  /api/config
 *   - POST /api/config
 *   - POST /api/ota
 *   - GET  /api/can/status
 *   - WS   /ws/telemetry
 *   - WS   /ws/events
 *
 * Quick validation examples (replace ${HOST} with the device IP):
 *   curl -sS http://${HOST}/api/status | jq
 *   curl -sS http://${HOST}/api/config | jq
 *   curl -sS -X POST http://${HOST}/api/config -H 'Content-Type: application/json' \
 *        -d '{"demo":true}'
 *   curl -sS -X POST http://${HOST}/api/ota -H 'Content-Type: multipart/form-data' \
 *        -F 'firmware=@tinybms_web_gateway.bin;type=application/octet-stream'
 *   curl -sS -X POST http://${HOST}/api/system/restart -d '{"target":"gateway"}' \
 *        -H 'Content-Type: application/json'
 */
void web_server_init(void);

/**
 * @brief Deinitialize the web server and free resources.
 */
void web_server_deinit(void);

/**
 * @brief Provide the event bus publisher so the server can emit notifications.
 */
void web_server_set_event_publisher(event_bus_publish_fn_t publisher);
