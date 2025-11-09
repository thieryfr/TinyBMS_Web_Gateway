/**
 * @file web_server_alerts.c
 * @brief Web server alert API endpoints and WebSocket handlers
 */

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "alert_manager.h"
#include "web_server.h"

#include <string.h>

static const char *TAG = "web_server_alerts";

#define ALERT_JSON_BUFFER_SIZE 8192

// WebSocket client list for alerts (defined in web_server.c)
extern ws_client_t *s_alert_clients;
extern httpd_handle_t s_httpd;
extern SemaphoreHandle_t s_ws_mutex;

// External WebSocket helper functions (from web_server.c)
extern void ws_client_list_add(ws_client_t **list, int fd);
extern void ws_client_list_remove(ws_client_t **list, int fd);
extern void ws_client_list_broadcast(ws_client_t **list, const char *payload, size_t length);

// =============================================================================
// API Handlers
// =============================================================================

/**
 * @brief GET /api/alerts/config - Get alert configuration
 */
esp_err_t web_server_api_alerts_config_get_handler(httpd_req_t *req)
{
    char *buffer = malloc(ALERT_JSON_BUFFER_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    esp_err_t err = alert_manager_get_config_json(buffer, ALERT_JSON_BUFFER_SIZE, &length);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get alert config JSON: %s", esp_err_to_name(err));
        free(buffer);
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, length);
    free(buffer);

    return ESP_OK;
}

/**
 * @brief POST /api/alerts/config - Update alert configuration
 */
esp_err_t web_server_api_alerts_config_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty payload");
        return ESP_FAIL;
    }

    if (req->content_len > ALERT_JSON_BUFFER_SIZE) {
        httpd_resp_send_err(req, HTTPD_413_PAYLOAD_TOO_LARGE, "Payload too large");
        return ESP_FAIL;
    }

    char *buffer = malloc(req->content_len + 1);
    if (buffer == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int ret = httpd_req_recv(req, buffer, req->content_len);
    if (ret <= 0) {
        free(buffer);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        } else {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }

    buffer[ret] = '\0';

    esp_err_t err = alert_manager_set_config_json(buffer, ret);
    free(buffer);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set alert config: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");

    return ESP_OK;
}

/**
 * @brief GET /api/alerts/active - Get active alerts
 */
esp_err_t web_server_api_alerts_active_handler(httpd_req_t *req)
{
    char *buffer = malloc(ALERT_JSON_BUFFER_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    esp_err_t err = alert_manager_get_active_alerts_json(buffer, ALERT_JSON_BUFFER_SIZE, &length);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get active alerts JSON: %s", esp_err_to_name(err));
        free(buffer);
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, length);
    free(buffer);

    return ESP_OK;
}

/**
 * @brief GET /api/alerts/history?limit=N - Get alert history
 */
esp_err_t web_server_api_alerts_history_handler(httpd_req_t *req)
{
    // Parse query parameter "limit"
    size_t limit = 0;
    char query_buf[32];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char param_value[16];
        if (httpd_query_key_value(query_buf, "limit", param_value, sizeof(param_value)) == ESP_OK) {
            limit = (size_t)atoi(param_value);
        }
    }

    char *buffer = malloc(ALERT_JSON_BUFFER_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    esp_err_t err = alert_manager_get_history_json(buffer, ALERT_JSON_BUFFER_SIZE, &length, limit);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get alert history JSON: %s", esp_err_to_name(err));
        free(buffer);
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, length);
    free(buffer);

    return ESP_OK;
}

/**
 * @brief POST /api/alerts/acknowledge/{id} - Acknowledge specific alert
 */
esp_err_t web_server_api_alerts_acknowledge_handler(httpd_req_t *req)
{
    // Extract alert ID from URI
    const char *uri = req->uri;
    const char *id_start = strrchr(uri, '/');
    if (id_start == NULL || id_start[1] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing alert ID");
        return ESP_FAIL;
    }

    uint32_t alert_id = (uint32_t)atoi(id_start + 1);

    esp_err_t err = alert_manager_acknowledge(alert_id);

    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Alert not found");
        return ESP_FAIL;
    }

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"acknowledged\"}");

    return ESP_OK;
}

/**
 * @brief POST /api/alerts/acknowledge - Acknowledge all alerts
 */
esp_err_t web_server_api_alerts_acknowledge_all_handler(httpd_req_t *req)
{
    esp_err_t err = alert_manager_acknowledge_all();

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"acknowledged\"}");

    return ESP_OK;
}

/**
 * @brief GET /api/alerts/statistics - Get alert statistics
 */
esp_err_t web_server_api_alerts_statistics_handler(httpd_req_t *req)
{
    alert_statistics_t stats;
    esp_err_t err = alert_manager_get_statistics(&stats);

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return err;
    }

    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer),
                       "{\"total_alerts\":%lu,"
                       "\"active_count\":%lu,"
                       "\"critical_count\":%lu,"
                       "\"warning_count\":%lu,"
                       "\"info_count\":%lu,"
                       "\"total_acknowledged\":%lu}",
                       stats.total_alerts_triggered,
                       stats.active_alert_count,
                       stats.critical_count,
                       stats.warning_count,
                       stats.info_count,
                       stats.total_acknowledged);

    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, len);

    return ESP_OK;
}

/**
 * @brief DELETE /api/alerts/history - Clear alert history
 */
esp_err_t web_server_api_alerts_clear_history_handler(httpd_req_t *req)
{
    esp_err_t err = alert_manager_clear_history();

    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"cleared\"}");

    return ESP_OK;
}

// =============================================================================
// WebSocket Handler
// =============================================================================

/**
 * @brief WebSocket handler for /ws/alerts
 *
 * Provides real-time alert notifications to connected clients.
 */
esp_err_t web_server_ws_alerts_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket /ws/alerts: new connection (fd=%d)", httpd_req_to_sockfd(req));
        ws_client_list_add(&s_alert_clients, httpd_req_to_sockfd(req));

        // Send initial connection message
        const char *welcome_msg = "{\"type\":\"alerts\",\"status\":\"connected\"}";
        httpd_ws_frame_t welcome_frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)welcome_msg,
            .len = strlen(welcome_msg),
        };
        httpd_ws_send_frame(req, &welcome_frame);

        return ESP_OK;
    }

    // Handle incoming WebSocket frames (PING/PONG)
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket /ws/alerts receive error: %s", esp_err_to_name(err));
        ws_client_list_remove(&s_alert_clients, httpd_req_to_sockfd(req));
        return err;
    }

    if (frame.len == 0) {
        return ESP_OK;
    }

    // Validate incoming payload size to prevent DoS attacks
    if (frame.len > WEB_SERVER_WS_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "WebSocket /ws/alerts: payload too large (%zu bytes > %d max), rejecting",
                 frame.len, WEB_SERVER_WS_MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Allocate buffer for payload
    uint8_t *buf = calloc(1, frame.len + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "WebSocket /ws/alerts: failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }

    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket /ws/alerts frame receive error: %s", esp_err_to_name(err));
        free(buf);
        return err;
    }

    // Handle PING/PONG
    if (frame.type == HTTPD_WS_TYPE_PING) {
        frame.type = HTTPD_WS_TYPE_PONG;
        httpd_ws_send_frame(req, &frame);
        ESP_LOGD(TAG, "WebSocket /ws/alerts: PONG sent");
    }

    free(buf);
    return ESP_OK;
}

// =============================================================================
// Alert Broadcasting (called by alert_manager via event bus)
// =============================================================================

/**
 * @brief Broadcast alert to all connected WebSocket clients
 *
 * This function should be called whenever a new alert is triggered or
 * an alert status changes.
 */
void web_server_broadcast_alert(const char *alert_json)
{
    if (alert_json == NULL) {
        return;
    }

    ws_client_list_broadcast(&s_alert_clients, alert_json, strlen(alert_json));
}
