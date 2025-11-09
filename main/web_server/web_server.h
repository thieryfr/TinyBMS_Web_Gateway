#pragma once

#include "event_bus.h"

/**
 * @brief Initialise the embedded HTTP server and register REST/WebSocket handlers.
 *
 * Endpoints exposed by the server:
 *   - GET  /api/status
 *   - GET  /api/config
 *   - POST /api/config
 *   - POST /api/ota
 *   - WS   /ws/telemetry
 *   - WS   /ws/events
 *
 * Quick validation examples (replace ${HOST} with the device IP):
 *   curl -sS http://${HOST}/api/status | jq
 *   curl -sS http://${HOST}/api/config | jq
 *   curl -sS -X POST http://${HOST}/api/config -H 'Content-Type: application/json' \
 *        -d '{"demo":true}'
 *   curl -sS -X POST http://${HOST}/api/ota --data-binary @firmware.bin
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
