/**
 * @file web_server_internal.h
 * @brief Internal header for web server module (shared across split files)
 *
 * This header is used internally by the web server module components.
 * It contains declarations for functions and data structures that are
 * shared across the split web_server files.
 */

#ifndef WEB_SERVER_INTERNAL_H
#define WEB_SERVER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration constants
// ============================================================================

#define WEB_SERVER_MUTEX_TIMEOUT_MS 5000
#define WEB_SERVER_MAX_URI_LEN 128
#define WEB_SERVER_MAX_CONTENT_LEN 8192

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
#define WEB_SERVER_AUTH_HEADER_MAX 512
#define WEB_SERVER_AUTH_DECODED_MAX 256
#define WEB_SERVER_AUTH_MAX_USERNAME_LENGTH 32
#define WEB_SERVER_AUTH_MAX_PASSWORD_LENGTH 64
#define WEB_SERVER_AUTH_SALT_SIZE 32
#define WEB_SERVER_AUTH_HASH_SIZE 32
#define WEB_SERVER_CSRF_TOKEN_SIZE 32
#define WEB_SERVER_CSRF_TOKEN_STRING_LENGTH 64
#define WEB_SERVER_CSRF_MAX_TOKENS 8
#define WEB_SERVER_CSRF_TOKEN_TTL_MS 300000  // 5 minutes
#endif

// ============================================================================
// External state (from web_server_core.c)
// ============================================================================

extern httpd_handle_t g_server;
extern SemaphoreHandle_t g_server_mutex;
extern event_bus_publish_fn_t g_event_publisher;

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
extern SemaphoreHandle_t g_auth_mutex;
extern bool g_basic_auth_enabled;
#endif

// ============================================================================
// Utility functions (from web_server_core.c)
// ============================================================================

/**
 * @brief Set security headers on HTTP response
 */
void web_server_set_security_headers(httpd_req_t *req);

/**
 * @brief Take server mutex with timeout
 */
bool web_server_lock(TickType_t timeout);

/**
 * @brief Release server mutex
 */
void web_server_unlock(void);

// ============================================================================
// Authentication functions (from web_server_auth.c)
// ============================================================================

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE

/**
 * @brief Initialize authentication module
 */
void web_server_auth_init(void);

/**
 * @brief Require authorization (Basic Auth + optional CSRF)
 *
 * @param req HTTP request
 * @param require_csrf Whether CSRF token is required
 * @param out_username Buffer for username (optional)
 * @param out_size Size of username buffer
 * @return true if authorized, false otherwise
 */
bool web_server_require_authorization(httpd_req_t *req, bool require_csrf,
                                      char *out_username, size_t out_size);

/**
 * @brief Send 401 Unauthorized response
 */
void web_server_send_unauthorized(httpd_req_t *req);

/**
 * @brief Send 403 Forbidden response
 */
void web_server_send_forbidden(httpd_req_t *req, const char *message);

/**
 * @brief Issue new CSRF token
 */
bool web_server_issue_csrf_token(const char *username, char *out_token,
                                 size_t token_size, uint32_t *out_ttl_ms);

#else
// Stub implementations when auth disabled
static inline void web_server_auth_init(void) {}
static inline bool web_server_require_authorization(httpd_req_t *req, bool require_csrf,
                                                    char *out_username, size_t out_size) {
    (void)req; (void)require_csrf; (void)out_username; (void)out_size;
    return true;
}
#endif

// ============================================================================
// API handlers (from web_server_api.c)
// ============================================================================

esp_err_t web_server_api_status_handler(httpd_req_t *req);
esp_err_t web_server_api_config_get_handler(httpd_req_t *req);
esp_err_t web_server_api_config_post_handler(httpd_req_t *req);
esp_err_t web_server_api_mqtt_config_get_handler(httpd_req_t *req);
esp_err_t web_server_api_mqtt_config_post_handler(httpd_req_t *req);
esp_err_t web_server_api_ota_post_handler(httpd_req_t *req);
esp_err_t web_server_api_restart_post_handler(httpd_req_t *req);
esp_err_t web_server_api_metrics_runtime_handler(httpd_req_t *req);
esp_err_t web_server_api_event_bus_metrics_handler(httpd_req_t *req);
esp_err_t web_server_api_system_tasks_handler(httpd_req_t *req);
esp_err_t web_server_api_system_modules_handler(httpd_req_t *req);

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
esp_err_t web_server_api_security_csrf_get_handler(httpd_req_t *req);
#endif

// ============================================================================
// Static file handlers (from web_server_static.c)
// ============================================================================

esp_err_t web_server_static_get_handler(httpd_req_t *req);

// ============================================================================
// WebSocket handlers (from web_server_websocket.c)
// ============================================================================

esp_err_t web_server_telemetry_ws_handler(httpd_req_t *req);
esp_err_t web_server_events_ws_handler(httpd_req_t *req);
esp_err_t web_server_uart_ws_handler(httpd_req_t *req);
esp_err_t web_server_can_ws_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif  // WEB_SERVER_INTERNAL_H
