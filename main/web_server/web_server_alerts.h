#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// API Handlers
// =============================================================================

/**
 * @brief GET /api/alerts/config - Get alert configuration
 */
esp_err_t web_server_api_alerts_config_get_handler(httpd_req_t *req);

/**
 * @brief POST /api/alerts/config - Update alert configuration
 */
esp_err_t web_server_api_alerts_config_post_handler(httpd_req_t *req);

/**
 * @brief GET /api/alerts/active - Get active alerts
 */
esp_err_t web_server_api_alerts_active_handler(httpd_req_t *req);

/**
 * @brief GET /api/alerts/history?limit=N - Get alert history
 */
esp_err_t web_server_api_alerts_history_handler(httpd_req_t *req);

/**
 * @brief POST /api/alerts/acknowledge/{id} - Acknowledge specific alert
 */
esp_err_t web_server_api_alerts_acknowledge_handler(httpd_req_t *req);

/**
 * @brief POST /api/alerts/acknowledge - Acknowledge all alerts
 */
esp_err_t web_server_api_alerts_acknowledge_all_handler(httpd_req_t *req);

/**
 * @brief GET /api/alerts/statistics - Get alert statistics
 */
esp_err_t web_server_api_alerts_statistics_handler(httpd_req_t *req);

/**
 * @brief DELETE /api/alerts/history - Clear alert history
 */
esp_err_t web_server_api_alerts_clear_history_handler(httpd_req_t *req);

// =============================================================================
// WebSocket Handler
// =============================================================================

/**
 * @brief WebSocket handler for /ws/alerts
 *
 * Provides real-time alert notifications to connected clients.
 */
esp_err_t web_server_ws_alerts_handler(httpd_req_t *req);

// =============================================================================
// Broadcasting
// =============================================================================

/**
 * @brief Broadcast alert to all connected WebSocket clients
 * @param alert_json JSON string representing the alert
 */
void web_server_broadcast_alert(const char *alert_json);

#ifdef __cplusplus
}
#endif
