#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

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
 *   - GET  /api/config (snapshot public; ajouter `?include_secrets=1` pour le snapshot complet si autorisé)
 *   - POST /api/config
 *   - GET  /api/security/csrf (récupération du jeton CSRF synchronisé)
 *   - POST /api/ota
 *   - GET  /api/can/status
 *   - WS   /ws/telemetry
 *   - WS   /ws/events
 *
 * Les routes sensibles (configuration, OTA, redémarrage, MQTT) sont protégées par
 * une authentification HTTP Basic. Les requêtes modifiant l'état doivent en plus
 * inclure l'en-tête `X-CSRF-Token` obtenu via `/api/security/csrf`.
 *
 * Quick validation examples (replace ${HOST} with the device IP):
 *   curl -su admin:changeme http://${HOST}/api/status | jq
 *   curl -su admin:changeme http://${HOST}/api/config | jq
 *   CSRF=$(curl -su admin:changeme http://${HOST}/api/security/csrf | jq -r '.token')
 *   curl -su admin:changeme -X POST http://${HOST}/api/config \
 *        -H "Content-Type: application/json" -H "X-CSRF-Token: ${CSRF}" \
 *        -d '{"demo":true}'
 *   curl -su admin:changeme -X POST http://${HOST}/api/ota \
 *        -H "Content-Type: multipart/form-data" -H "X-CSRF-Token: ${CSRF}" \
 *        -F 'firmware=@tinybms_web_gateway.bin;type=application/octet-stream'
 *   curl -su admin:changeme -X POST http://${HOST}/api/system/restart \
 *        -H "Content-Type: application/json" -H "X-CSRF-Token: ${CSRF}" \
 *        -d '{"target":"gateway"}'
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

typedef bool (*web_server_secret_authorizer_fn_t)(httpd_req_t *req);

void web_server_set_config_secret_authorizer(web_server_secret_authorizer_fn_t authorizer);

bool web_server_uri_requests_full_snapshot(const char *uri);

esp_err_t web_server_prepare_config_snapshot(const char *uri,
                                             bool authorized_for_secrets,
                                             char *buffer,
                                             size_t buffer_size,
                                             size_t *out_length,
                                             const char **visibility_out);
