/**
 * @file web_server_websocket.c
 * @brief WebSocket endpoints for real-time telemetry and events
 *
 * This file contains functionality for:
 * - WebSocket client management (add, remove, broadcast)
 * - Telemetry streaming (/ws/telemetry)
 * - Event streaming (/ws/events)
 * - UART data streaming (/ws/uart)
 * - CAN data streaming (/ws/can)
 * - Rate limiting and payload size validation
 */

#include "web_server.h"
#include "web_server_internal.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "monitoring.h"

// WebSocket rate limiting and security
#define WEB_SERVER_WS_RATE_WINDOW_MS      1000         // 1 second rate limiting window

static const char *TAG = "web_server_ws";

// ============================================================================
// WebSocket client structure
// ============================================================================

typedef struct ws_client {
    int fd;
    struct ws_client *next;
    // Rate limiting
    int64_t last_reset_time;      // Timestamp (ms) of rate window start
    uint32_t message_count;        // Messages sent in current window
    uint32_t total_violations;     // Total rate limit violations
} ws_client_t;

// ============================================================================
// Global state (WebSocket clients)
// ============================================================================

SemaphoreHandle_t g_server_mutex = NULL;  // Also used as ws_mutex
static ws_client_t *s_telemetry_clients = NULL;
static ws_client_t *s_event_clients = NULL;
static ws_client_t *s_uart_clients = NULL;
static ws_client_t *s_can_clients = NULL;
static ws_client_t *s_alert_clients = NULL;

// External reference to httpd handle from core
extern httpd_handle_t g_server;

// ============================================================================
// Client list management
// ============================================================================

/**
 * Free all clients in a WebSocket client list
 * @param list Pointer to the list head pointer
 */
static void ws_client_list_free(ws_client_t **list)
{
    if (list == NULL) {
        return;
    }

    ws_client_t *current = *list;
    while (current != NULL) {
        ws_client_t *next = current->next;
        free(current);
        current = next;
    }
    *list = NULL;
}

static void ws_client_list_add(ws_client_t **list, int fd)
{
    if (list == NULL || fd < 0 || g_server_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_server_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (ws_client_t *iter = *list; iter != NULL; iter = iter->next) {
        if (iter->fd == fd) {
            xSemaphoreGive(g_server_mutex);
            return;
        }
    }

    ws_client_t *client = calloc(1, sizeof(ws_client_t));
    if (client == NULL) {
        xSemaphoreGive(g_server_mutex);
        ESP_LOGW(TAG, "Unable to allocate memory for websocket client");
        return;
    }

    client->fd = fd;
    client->next = *list;
    // Initialize rate limiting (calloc already zeroed message_count and total_violations)
    client->last_reset_time = esp_timer_get_time() / 1000;  // Convert to ms
    *list = client;

    xSemaphoreGive(g_server_mutex);
}

static void ws_client_list_remove(ws_client_t **list, int fd)
{
    if (list == NULL || g_server_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_server_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    ws_client_t *prev = NULL;
    ws_client_t *iter = *list;
    while (iter != NULL) {
        if (iter->fd == fd) {
            if (prev == NULL) {
                *list = iter->next;
            } else {
                prev->next = iter->next;
            }
            free(iter);
            break;
        }
        prev = iter;
        iter = iter->next;
    }

    xSemaphoreGive(g_server_mutex);
}

static void ws_client_list_broadcast(ws_client_t **list, const char *payload, size_t length)
{
    if (list == NULL || payload == NULL || length == 0 || g_server_mutex == NULL || g_server == NULL) {
        return;
    }

    // Validate payload size to prevent DoS attacks
    if (length > WEB_SERVER_WS_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "WebSocket broadcast: payload too large (%zu bytes > %d max), dropping",
                 length, WEB_SERVER_WS_MAX_PAYLOAD_SIZE);
        return;
    }

    // Calculer la longueur du payload (sans le '\0' final si présent)
    size_t payload_length = length;
    if (payload_length > 0 && payload[payload_length - 1] == '\0') {
        payload_length -= 1;
    }

    if (payload_length == 0) {
        return;
    }

    // Copier la liste des FDs sous mutex pour minimiser la section critique
    #define MAX_BROADCAST_CLIENTS 32
    int client_fds[MAX_BROADCAST_CLIENTS];
    size_t client_count = 0;

    if (xSemaphoreTake(g_server_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "WebSocket broadcast: failed to acquire mutex (timeout), event dropped");
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000;  // Convert to ms
    ws_client_t *iter = *list;
    while (iter != NULL && client_count < MAX_BROADCAST_CLIENTS) {
        // Check and update rate limiting
        int64_t time_since_reset = current_time - iter->last_reset_time;

        // Reset rate window if expired
        if (time_since_reset >= WEB_SERVER_WS_RATE_WINDOW_MS) {
            iter->last_reset_time = current_time;
            iter->message_count = 0;
        }

        // Check rate limit
        if (iter->message_count >= WEB_SERVER_WS_MAX_MSGS_PER_SEC) {
            iter->total_violations++;
            if (iter->total_violations % 10 == 1) {  // Log every 10th violation to avoid spam
                ESP_LOGW(TAG, "WebSocket client fd=%d rate limited (%u msgs in window, %u total violations)",
                         iter->fd, iter->message_count, iter->total_violations);
            }
            iter = iter->next;
            continue;  // Skip this client
        }

        // Client is within rate limit, include in broadcast
        iter->message_count++;
        client_fds[client_count++] = iter->fd;
        iter = iter->next;
    }

    xSemaphoreGive(g_server_mutex);

    // Diffuser hors section critique pour éviter les blocages
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = payload_length,
    };

    for (size_t i = 0; i < client_count; i++) {
        esp_err_t err = httpd_ws_send_frame_async(g_server, client_fds[i], &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send to websocket client %d: %s", client_fds[i], esp_err_to_name(err));
            // Retirer le client en échec de la liste
            ws_client_list_remove(list, client_fds[i]);
        }
    }
}

static void web_server_broadcast_battery_snapshot(ws_client_t **list, const char *payload, size_t length)
{
    if (list == NULL || payload == NULL || length == 0) {
        return;
    }

    size_t payload_length = length;
    if (payload_length > 0U && payload[payload_length - 1U] == '\0') {
        payload_length -= 1U;
    }

    if (payload_length == 0U) {
        return;
    }

    if (payload_length >= MONITORING_SNAPSHOT_MAX_SIZE) {
        ESP_LOGW(TAG, "Telemetry snapshot too large to wrap (%zu bytes)", payload_length);
        return;
    }

    char wrapped[MONITORING_SNAPSHOT_MAX_SIZE + 32U];
    int written = snprintf(wrapped, sizeof(wrapped), "{\"battery\":%.*s}", (int)payload_length, payload);
    if (written <= 0 || (size_t)written >= sizeof(wrapped)) {
        ESP_LOGW(TAG, "Failed to wrap telemetry snapshot for broadcast");
        return;
    }

    ws_client_list_broadcast(list, wrapped, (size_t)written);
}

// ============================================================================
// WebSocket frame handling
// ============================================================================

static esp_err_t web_server_handle_ws_close(httpd_req_t *req, ws_client_t **list)
{
    int fd = httpd_req_to_sockfd(req);
    ws_client_list_remove(list, fd);
    ESP_LOGI(TAG, "WebSocket client %d disconnected", fd);
    return ESP_OK;
}

static esp_err_t web_server_ws_control_frame(httpd_req_t *req, httpd_ws_frame_t *frame)
{
    if (frame->type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_PONG,
            .payload = frame->payload,
            .len = frame->len,
        };
        return httpd_ws_send_frame(req, &response);
    }

    if (frame->type == HTTPD_WS_TYPE_CLOSE) {
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t web_server_ws_receive(httpd_req_t *req, ws_client_t **list)
{
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame length: %s", esp_err_to_name(err));
        return err;
    }

    // Validate incoming payload size to prevent DoS attacks
    if (frame.len > WEB_SERVER_WS_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "WebSocket receive: payload too large (%zu bytes > %d max), rejecting",
                 frame.len, WEB_SERVER_WS_MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (frame.len > 0) {
        frame.payload = calloc(1, frame.len + 1);
        if (frame.payload == NULL) {
            return ESP_ERR_NO_MEM;
        }
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) {
            free(frame.payload);
            ESP_LOGE(TAG, "Failed to read frame payload: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        free(frame.payload);
        return web_server_handle_ws_close(req, list);
    }

    err = web_server_ws_control_frame(req, &frame);
    if (err != ESP_OK) {
        free(frame.payload);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT && frame.payload != NULL) {
        ESP_LOGD(TAG, "WS message: %.*s", frame.len, frame.payload);
    }

    free(frame.payload);
    return ESP_OK;
}

// ============================================================================
// WebSocket endpoint handlers
// ============================================================================

esp_err_t web_server_telemetry_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_client_list_add(&s_telemetry_clients, fd);
        ESP_LOGI(TAG, "Telemetry WebSocket client connected: %d", fd);

        char buffer[MONITORING_SNAPSHOT_MAX_SIZE];
        size_t length = 0;
        if (monitoring_get_status_json(buffer, sizeof(buffer), &length) == ESP_OK) {
            httpd_ws_frame_t frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)buffer,
                .len = length,
            };
            httpd_ws_send_frame(req, &frame);
        }

        return ESP_OK;
    }

    return web_server_ws_receive(req, &s_telemetry_clients);
}

esp_err_t web_server_events_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_client_list_add(&s_event_clients, fd);
        ESP_LOGI(TAG, "Events WebSocket client connected: %d", fd);

        static const char k_ready_message[] = "{\"event\":\"connected\"}";
        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)k_ready_message,
            .len = sizeof(k_ready_message) - 1,
        };
        httpd_ws_send_frame(req, &frame);
        return ESP_OK;
    }

    return web_server_ws_receive(req, &s_event_clients);
}

esp_err_t web_server_uart_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_client_list_add(&s_uart_clients, fd);
        ESP_LOGI(TAG, "UART WebSocket client connected: %d", fd);

        static const char k_ready_message[] = "{\"type\":\"uart\",\"status\":\"connected\"}";
        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)k_ready_message,
            .len = sizeof(k_ready_message) - 1,
        };
        httpd_ws_send_frame(req, &frame);
        return ESP_OK;
    }

    return web_server_ws_receive(req, &s_uart_clients);
}

esp_err_t web_server_can_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_client_list_add(&s_can_clients, fd);
        ESP_LOGI(TAG, "CAN WebSocket client connected: %d", fd);

        static const char k_ready_message[] = "{\"type\":\"can\",\"status\":\"connected\"}";
        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)k_ready_message,
            .len = sizeof(k_ready_message) - 1,
        };
        httpd_ws_send_frame(req, &frame);
        return ESP_OK;
    }

    return web_server_ws_receive(req, &s_can_clients);
}

// ============================================================================
// Cleanup function (called from web_server_core.c)
// ============================================================================

void web_server_websocket_cleanup(void)
{
    if (g_server_mutex != NULL) {
        if (xSemaphoreTake(g_server_mutex, pdMS_TO_TICKS(WEB_SERVER_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            ws_client_list_free(&s_telemetry_clients);
            ws_client_list_free(&s_event_clients);
            ws_client_list_free(&s_uart_clients);
            ws_client_list_free(&s_can_clients);
            ws_client_list_free(&s_alert_clients);
            xSemaphoreGive(g_server_mutex);
        }
    }
}

// ============================================================================
// Event broadcast function (called from event task in core)
// ============================================================================

void web_server_websocket_broadcast_event(uint32_t event_id, const char *payload, size_t length)
{
    if (payload == NULL || length == 0) {
        return;
    }

    switch (event_id) {
    case APP_EVENT_ID_TELEMETRY_SAMPLE:
        web_server_broadcast_battery_snapshot(&s_telemetry_clients, payload, length);
        break;
    case APP_EVENT_ID_UI_NOTIFICATION:
    case APP_EVENT_ID_CONFIG_UPDATED:
    case APP_EVENT_ID_OTA_UPLOAD_READY:
    case APP_EVENT_ID_MONITORING_DIAGNOSTICS:
        ws_client_list_broadcast(&s_event_clients, payload, length);
        break;
    case APP_EVENT_ID_WIFI_STA_START:
    case APP_EVENT_ID_WIFI_STA_CONNECTED:
    case APP_EVENT_ID_WIFI_STA_DISCONNECTED:
    case APP_EVENT_ID_WIFI_STA_GOT_IP:
    case APP_EVENT_ID_WIFI_STA_LOST_IP:
    case APP_EVENT_ID_WIFI_AP_STARTED:
    case APP_EVENT_ID_WIFI_AP_STOPPED:
    case APP_EVENT_ID_WIFI_AP_FAILED:
    case APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED:
    case APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED:
    case APP_EVENT_ID_STORAGE_HISTORY_READY:
    case APP_EVENT_ID_STORAGE_HISTORY_UNAVAILABLE:
        ws_client_list_broadcast(&s_event_clients, payload, length);
        break;
    case APP_EVENT_ID_UART_FRAME_RAW:
    case APP_EVENT_ID_UART_FRAME_DECODED:
        ws_client_list_broadcast(&s_uart_clients, payload, length);
        break;
    case APP_EVENT_ID_CAN_FRAME_RAW:
    case APP_EVENT_ID_CAN_FRAME_DECODED:
        ws_client_list_broadcast(&s_can_clients, payload, length);
        break;
    case APP_EVENT_ID_ALERT_TRIGGERED:
        ws_client_list_broadcast(&s_alert_clients, payload, length);
        break;
    default:
        break;
    }
}
