/**
 * @file web_server.c
 * @brief Core web server module - initialization, configuration, and shared utilities
 *
 * This is the core module of the web server, containing initialization/cleanup logic
 * and shared utility functions. Handler implementations have been split into:
 * - web_server_auth.c: Authentication and CSRF handling
 * - web_server_api.c: REST API endpoints
 * - web_server_static.c: Static file serving
 * - web_server_websocket.c: WebSocket endpoints
 */

#include "web_server.h"
#include "web_server_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "app_events.h"
#include "config_manager.h"
#include "monitoring.h"
#include "mqtt_gateway.h"
#include "mqtt_client.h"
#include "history_logger.h"
#include "history_fs.h"
#include "alert_manager.h"
#include "web_server_alerts.h"
#include "auth_rate_limit.h"
#include "can_victron.h"
#include "system_metrics.h"
#include "ota_update.h"
#include "system_control.h"
#include "web_server_ota_errors.h"

#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"

// ============================================================================
// HTTP status code definitions
// ============================================================================

#ifndef HTTPD_413_PAYLOAD_TOO_LARGE
#define HTTPD_413_PAYLOAD_TOO_LARGE 413
#endif

#ifndef HTTPD_414_URI_TOO_LONG
#define HTTPD_414_URI_TOO_LONG 414
#endif

#ifndef HTTPD_503_SERVICE_UNAVAILABLE
#define HTTPD_503_SERVICE_UNAVAILABLE 503
#endif

#ifndef HTTPD_415_UNSUPPORTED_MEDIA_TYPE
#define HTTPD_415_UNSUPPORTED_MEDIA_TYPE 415
#endif

#ifndef HTTPD_401_UNAUTHORIZED
#define HTTPD_401_UNAUTHORIZED 401
#endif

#ifndef HTTPD_403_FORBIDDEN
#define HTTPD_403_FORBIDDEN 403
#endif

// ============================================================================
// Web server constants
// ============================================================================

#define WEB_SERVER_FS_BASE_PATH "/spiffs"
#define WEB_SERVER_WEB_ROOT     WEB_SERVER_FS_BASE_PATH
#define WEB_SERVER_INDEX_PATH   WEB_SERVER_WEB_ROOT "/index.html"
#define WEB_SERVER_MAX_PATH     256
#define WEB_SERVER_FILE_BUFSZ   1024
#define WEB_SERVER_MULTIPART_BUFFER_SIZE 2048
#define WEB_SERVER_MULTIPART_BOUNDARY_MAX 72
#define WEB_SERVER_MULTIPART_HEADER_MAX 256
#define WEB_SERVER_RESTART_DEFAULT_DELAY_MS 750U
#define WEB_SERVER_HISTORY_JSON_SIZE      4096
#define WEB_SERVER_MQTT_JSON_SIZE         768
#define WEB_SERVER_CAN_JSON_SIZE          512
#define WEB_SERVER_RUNTIME_JSON_SIZE      1536
#define WEB_SERVER_EVENT_BUS_JSON_SIZE    1536
#define WEB_SERVER_TASKS_JSON_SIZE        8192
#define WEB_SERVER_MODULES_JSON_SIZE      2048
#define WEB_SERVER_JSON_CHUNK_SIZE        1024

#define WEB_SERVER_AUTH_NAMESPACE              "web_auth"
#define WEB_SERVER_AUTH_USERNAME_KEY           "username"
#define WEB_SERVER_AUTH_SALT_KEY               "salt"
#define WEB_SERVER_AUTH_HASH_KEY               "password_hash"
#define WEB_SERVER_AUTH_MAX_USERNAME_LENGTH    32
#define WEB_SERVER_AUTH_MAX_PASSWORD_LENGTH    64
#define WEB_SERVER_AUTH_SALT_SIZE              16
#define WEB_SERVER_AUTH_HASH_SIZE              32
#define WEB_SERVER_AUTH_HEADER_MAX             192
#define WEB_SERVER_MUTEX_TIMEOUT_MS            5000  // Timeout 5s pour éviter deadlock
#define WEB_SERVER_AUTH_DECODED_MAX            96
#define WEB_SERVER_CSRF_TOKEN_SIZE             32
#define WEB_SERVER_CSRF_TOKEN_STRING_LENGTH    (WEB_SERVER_CSRF_TOKEN_SIZE * 2)
#define WEB_SERVER_CSRF_TOKEN_TTL_US           (15ULL * 60ULL * 1000000ULL)
#define WEB_SERVER_MAX_CSRF_TOKENS             8

// WebSocket rate limiting and security
#define WEB_SERVER_WS_MAX_PAYLOAD_SIZE    (32 * 1024)  // 32KB max payload
#define WEB_SERVER_WS_MAX_MSGS_PER_SEC    10           // Max 10 messages/sec per client
#define WEB_SERVER_WS_RATE_WINDOW_MS      1000         // 1 second rate limiting window

// ============================================================================
// Type definitions
// ============================================================================

typedef struct ws_client {
    int fd;
    struct ws_client *next;
    // Rate limiting
    int64_t last_reset_time;      // Timestamp (ms) of rate window start
    uint32_t message_count;        // Messages sent in current window
    uint32_t total_violations;     // Total rate limit violations
} ws_client_t;

typedef struct {
    bool in_use;
    char username[WEB_SERVER_AUTH_MAX_USERNAME_LENGTH + 1];
    char token[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH + 1];
    int64_t expires_at_us;
} web_server_csrf_token_t;

// ============================================================================
// Module state
// ============================================================================

static const char *TAG = "web_server";

static const char *web_server_twai_state_to_string(twai_state_t state)
{
    switch (state) {
    case TWAI_STATE_STOPPED:
        return "Arrêté";
    case TWAI_STATE_RUNNING:
        return "En marche";
    case TWAI_STATE_BUS_OFF:
        return "Bus-off";
    case TWAI_STATE_RECOVERING:
        return "Récupération";
    default:
        return "Inconnu";
    }
}

static event_bus_publish_fn_t s_event_publisher = NULL;
static httpd_handle_t s_httpd = NULL;
static SemaphoreHandle_t s_ws_mutex = NULL;
static event_bus_subscription_handle_t s_event_subscription = NULL;
static TaskHandle_t s_event_task_handle = NULL;
static volatile bool s_event_task_should_stop = false;
static web_server_secret_authorizer_fn_t s_config_secret_authorizer = NULL;
static char s_ota_event_label[128];
static app_event_metadata_t s_ota_event_metadata = {
    .event_id = APP_EVENT_ID_OTA_UPLOAD_READY,
    .key = "ota_ready",
    .type = "ota",
    .label = s_ota_event_label,
    .timestamp_ms = 0U,
};
static char s_restart_event_label[128];
static app_event_metadata_t s_restart_event_metadata = {
    .event_id = APP_EVENT_ID_UI_NOTIFICATION,
    .key = "system_restart",
    .type = "system",
    .label = s_restart_event_label,
    .timestamp_ms = 0U,
};
static bool s_basic_auth_enabled = false;
static char s_basic_auth_username[WEB_SERVER_AUTH_MAX_USERNAME_LENGTH + 1];
static uint8_t s_basic_auth_salt[WEB_SERVER_AUTH_SALT_SIZE];
static uint8_t s_basic_auth_hash[WEB_SERVER_AUTH_HASH_SIZE];
static SemaphoreHandle_t s_auth_mutex = NULL;
static web_server_csrf_token_t s_csrf_tokens[WEB_SERVER_MAX_CSRF_TOKENS];

// ============================================================================
// Shared utility functions (used by split modules)
// ============================================================================

/**
 * Set security headers on HTTP response to prevent common web vulnerabilities
 * @param req HTTP request handle
 */
static void web_server_set_security_headers(httpd_req_t *req)
{
    // Content Security Policy - restrict resource loading to prevent XSS
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                      "default-src 'self'; "
                      "script-src 'self' 'unsafe-inline'; "
                      "style-src 'self' 'unsafe-inline'; "
                      "img-src 'self' data:; "
                      "connect-src 'self' ws: wss:; "
                      "font-src 'self'; "
                      "object-src 'none'; "
                      "base-uri 'self'; "
                      "form-action 'self'");

    // Prevent clickjacking attacks
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");

    // Prevent MIME sniffing
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");

    // Enable XSS protection in older browsers
    httpd_resp_set_hdr(req, "X-XSS-Protection", "1; mode=block");

    // Referrer policy - don't leak URLs
    httpd_resp_set_hdr(req, "Referrer-Policy", "strict-origin-when-cross-origin");

    // Permissions policy - disable unnecessary features
    httpd_resp_set_hdr(req, "Permissions-Policy",
                      "accelerometer=(), camera=(), geolocation=(), gyroscope=(), "
                      "magnetometer=(), microphone=(), payment=(), usb=()");
}

static bool web_server_format_iso8601(time_t timestamp, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }

    if (timestamp <= 0) {
        buffer[0] = '\0';
        return false;
    }

    struct tm tm_utc;
    if (gmtime_r(&timestamp, &tm_utc) == NULL) {
        buffer[0] = '\0';
        return false;
    }

    size_t written = strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    if (written == 0) {
        buffer[0] = '\0';
        return false;
    }

    return true;
}

static esp_err_t web_server_send_json(httpd_req_t *req, const char *buffer, size_t length)
{
    if (req == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    web_server_set_security_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    size_t offset = 0U;
    while (offset < length) {
        size_t remaining = length - offset;
        size_t chunk = (remaining > WEB_SERVER_JSON_CHUNK_SIZE) ? WEB_SERVER_JSON_CHUNK_SIZE : remaining;

        esp_err_t err = httpd_resp_send_chunk(req, buffer + offset, chunk);
        if (err != ESP_OK) {
            return err;
        }

        offset += chunk;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

// ============================================================================
// Forward declarations for event task
// ============================================================================

static void web_server_event_task(void *context);

// ============================================================================
// Public API functions
// ============================================================================

void web_server_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void web_server_set_config_secret_authorizer(web_server_secret_authorizer_fn_t authorizer)
{
    s_config_secret_authorizer = authorizer;
}

void web_server_init(void)
{
    if (s_ws_mutex == NULL) {
        s_ws_mutex = xSemaphoreCreateMutex();
    }

    if (s_ws_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create websocket mutex");
        return;
    }

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
    web_server_auth_init();
    if (!s_basic_auth_enabled) {
        ESP_LOGW(TAG, "HTTP authentication is not available; protected endpoints will reject requests");
    }
#endif

    esp_err_t err = web_server_mount_spiffs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Serving static assets from SPIFFS disabled");
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return;
    }

    const httpd_uri_t api_metrics_runtime = {
        .uri = "/api/metrics/runtime",
        .method = HTTP_GET,
        .handler = web_server_api_metrics_runtime_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_metrics_runtime);

    const httpd_uri_t api_event_bus_metrics = {
        .uri = "/api/event-bus/metrics",
        .method = HTTP_GET,
        .handler = web_server_api_event_bus_metrics_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_event_bus_metrics);

    const httpd_uri_t api_system_tasks = {
        .uri = "/api/system/tasks",
        .method = HTTP_GET,
        .handler = web_server_api_system_tasks_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_system_tasks);

    const httpd_uri_t api_system_modules = {
        .uri = "/api/system/modules",
        .method = HTTP_GET,
        .handler = web_server_api_system_modules_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_system_modules);

    const httpd_uri_t api_system_restart = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler = web_server_api_restart_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_system_restart);

    const httpd_uri_t api_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = web_server_api_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_status);

    const httpd_uri_t api_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = web_server_api_config_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_config_get);

    const httpd_uri_t api_config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = web_server_api_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_config_post);

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
    const httpd_uri_t api_security_csrf = {
        .uri = "/api/security/csrf",
        .method = HTTP_GET,
        .handler = web_server_api_security_csrf_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_security_csrf);
#endif

    const httpd_uri_t api_mqtt_config_get = {
        .uri = "/api/mqtt/config",
        .method = HTTP_GET,
        .handler = web_server_api_mqtt_config_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_mqtt_config_get);

    const httpd_uri_t api_mqtt_config_post = {
        .uri = "/api/mqtt/config",
        .method = HTTP_POST,
        .handler = web_server_api_mqtt_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_mqtt_config_post);

    const httpd_uri_t api_mqtt_status = {
        .uri = "/api/mqtt/status",
        .method = HTTP_GET,
        .handler = web_server_api_mqtt_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_mqtt_status);

    const httpd_uri_t api_mqtt_test = {
        .uri = "/api/mqtt/test",
        .method = HTTP_GET,
        .handler = web_server_api_mqtt_test_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_mqtt_test);

    const httpd_uri_t api_can_status = {
        .uri = "/api/can/status",
        .method = HTTP_GET,
        .handler = web_server_api_can_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_can_status);

    const httpd_uri_t api_history = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = web_server_api_history_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_history);

    const httpd_uri_t api_history_files = {
        .uri = "/api/history/files",
        .method = HTTP_GET,
        .handler = web_server_api_history_files_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_history_files);

    const httpd_uri_t api_history_archive = {
        .uri = "/api/history/archive",
        .method = HTTP_GET,
        .handler = web_server_api_history_archive_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_history_archive);

    const httpd_uri_t api_history_download = {
        .uri = "/api/history/download",
        .method = HTTP_GET,
        .handler = web_server_api_history_download_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_history_download);

    const httpd_uri_t api_registers_get = {
        .uri = "/api/registers",
        .method = HTTP_GET,
        .handler = web_server_api_registers_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_registers_get);

    const httpd_uri_t api_registers_post = {
        .uri = "/api/registers",
        .method = HTTP_POST,
        .handler = web_server_api_registers_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_registers_post);

    const httpd_uri_t api_ota_post = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = web_server_api_ota_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_ota_post);

    // Alert API endpoints
    const httpd_uri_t api_alerts_config_get = {
        .uri = "/api/alerts/config",
        .method = HTTP_GET,
        .handler = web_server_api_alerts_config_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_config_get);

    const httpd_uri_t api_alerts_config_post = {
        .uri = "/api/alerts/config",
        .method = HTTP_POST,
        .handler = web_server_api_alerts_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_config_post);

    const httpd_uri_t api_alerts_active = {
        .uri = "/api/alerts/active",
        .method = HTTP_GET,
        .handler = web_server_api_alerts_active_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_active);

    const httpd_uri_t api_alerts_history = {
        .uri = "/api/alerts/history",
        .method = HTTP_GET,
        .handler = web_server_api_alerts_history_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_history);

    const httpd_uri_t api_alerts_ack = {
        .uri = "/api/alerts/acknowledge",
        .method = HTTP_POST,
        .handler = web_server_api_alerts_acknowledge_all_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_ack);

    const httpd_uri_t api_alerts_ack_id = {
        .uri = "/api/alerts/acknowledge/*",
        .method = HTTP_POST,
        .handler = web_server_api_alerts_acknowledge_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_ack_id);

    const httpd_uri_t api_alerts_stats = {
        .uri = "/api/alerts/statistics",
        .method = HTTP_GET,
        .handler = web_server_api_alerts_statistics_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_stats);

    const httpd_uri_t api_alerts_clear = {
        .uri = "/api/alerts/history",
        .method = HTTP_DELETE,
        .handler = web_server_api_alerts_clear_history_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_alerts_clear);

    const httpd_uri_t telemetry_ws = {
        .uri = "/ws/telemetry",
        .method = HTTP_GET,
        .handler = web_server_telemetry_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_httpd, &telemetry_ws);

    const httpd_uri_t events_ws = {
        .uri = "/ws/events",
        .method = HTTP_GET,
        .handler = web_server_events_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_httpd, &events_ws);

    const httpd_uri_t uart_ws = {
        .uri = "/ws/uart",
        .method = HTTP_GET,
        .handler = web_server_uart_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_httpd, &uart_ws);

    const httpd_uri_t can_ws = {
        .uri = "/ws/can",
        .method = HTTP_GET,
        .handler = web_server_can_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_httpd, &can_ws);

    const httpd_uri_t ws_alerts = {
        .uri = "/ws/alerts",
        .method = HTTP_GET,
        .handler = web_server_ws_alerts_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    httpd_register_uri_handler(s_httpd, &ws_alerts);

    const httpd_uri_t static_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = web_server_static_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &static_files);

    // Initialize alert manager
    alert_manager_init();
    if (s_event_publisher != NULL) {
        alert_manager_set_event_publisher(s_event_publisher);
    }

    s_event_subscription = event_bus_subscribe_default_named("web_server", NULL, NULL);
    if (s_event_subscription == NULL) {
        ESP_LOGW(TAG, "Failed to subscribe to event bus; WebSocket forwarding disabled");
        return;
    }

    // Pass current task handle so event task can notify us when it exits
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (xTaskCreate(web_server_event_task, "ws_event", 4096, (void *)current_task, 5, &s_event_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start event dispatcher task");
    }
}

void web_server_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing web server...");

    // Signal event task to exit
    s_event_task_should_stop = true;

    // Wait for event task to exit cleanly (max 5 seconds)
    if (s_event_task_handle != NULL) {
        ESP_LOGI(TAG, "Waiting for event task to exit...");
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
            ESP_LOGW(TAG, "Event task did not exit within timeout");
        } else {
            ESP_LOGI(TAG, "Event task exited cleanly");
        }
    }

    // Now safe to stop HTTP server
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }

    // Cleanup WebSocket client lists (delegated to websocket module)
    web_server_websocket_cleanup();

    // Unsubscribe from event bus
    if (s_event_subscription != NULL) {
        event_bus_unsubscribe(s_event_subscription);
        s_event_subscription = NULL;
    }

    // Destroy websocket mutex
    if (s_ws_mutex != NULL) {
        vSemaphoreDelete(s_ws_mutex);
        s_ws_mutex = NULL;
    }

    // Unmount SPIFFS (may already be unmounted by config_manager)
    esp_err_t err = esp_vfs_spiffs_unregister(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to unmount SPIFFS: %s", esp_err_to_name(err));
    }

    // Reset state
    s_event_task_handle = NULL;
    s_event_task_should_stop = false;
    s_event_publisher = NULL;

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
    if (s_auth_mutex != NULL) {
        vSemaphoreDelete(s_auth_mutex);
        s_auth_mutex = NULL;
    }
    s_basic_auth_enabled = false;
    mbedtls_platform_zeroize(s_basic_auth_username, sizeof(s_basic_auth_username));
    mbedtls_platform_zeroize(s_basic_auth_salt, sizeof(s_basic_auth_salt));
    mbedtls_platform_zeroize(s_basic_auth_hash, sizeof(s_basic_auth_hash));
    memset(s_csrf_tokens, 0, sizeof(s_csrf_tokens));
#endif

    ESP_LOGI(TAG, "Web server deinitialized");
}

// ============================================================================
// Event task (forwards events to WebSocket clients)
// ============================================================================

static void web_server_event_task(void *context)
{
    TaskHandle_t parent_task = (TaskHandle_t)context;
    ESP_LOGI(TAG, "Event task started");

    if (s_event_subscription == NULL) {
        ESP_LOGE(TAG, "Event task started without valid subscription");
        if (parent_task != NULL) {
            xTaskNotifyGive(parent_task);
        }
        vTaskDelete(NULL);
        return;
    }

    while (!s_event_task_should_stop) {
        app_event_t event;
        if (!event_bus_receive(s_event_subscription, &event, pdMS_TO_TICKS(100))) {
            continue;
        }

        const char *payload = NULL;
        size_t length = 0U;
        char generated_payload[384];

        if (event.metadata != NULL) {
            const app_event_metadata_t *meta = (const app_event_metadata_t *)event.metadata;
            if (meta->key != NULL && meta->type != NULL && meta->label != NULL) {
                int written = snprintf(generated_payload,
                                       sizeof(generated_payload),
                                       "{\"event_id\":%u,\"key\":\"%s\",\"type\":\"%s\",\"label\":\"%s\"",
                                       (unsigned)event.id,
                                       meta->key,
                                       meta->type,
                                       meta->label);
                if (written > 0 && (size_t)written < sizeof(generated_payload)) {
                    size_t used = (size_t)written;
                    if (meta->timestamp_ms > 0ULL) {
                        int ts = snprintf(generated_payload + used,
                                          sizeof(generated_payload) - used,
                                          ",\"timestamp\":%llu",
                                          meta->timestamp_ms);
                        if (ts > 0 && (size_t)ts < sizeof(generated_payload) - used) {
                            used += (size_t)ts;
                        }
                    }
                    if (used < sizeof(generated_payload)) {
                        int closed = snprintf(generated_payload + used,
                                              sizeof(generated_payload) - used,
                                              "}");
                        if (closed > 0 && (size_t)closed < sizeof(generated_payload) - used) {
                            used += (size_t)closed;
                            payload = generated_payload;
                            length = used;
                        }
                    }
                }
            }
        } else if (event.payload != NULL && event.payload_size > 0U) {
            payload = (const char *)event.payload;
            length = event.payload_size;
            if (length > 0U && payload[length - 1U] == '\0') {
                length -= 1U;
            }
        } else {
            int written = snprintf(generated_payload,
                                   sizeof(generated_payload),
                                   "{\"event_id\":%u}",
                                   (unsigned)event.id);
            if (written > 0 && (size_t)written < sizeof(generated_payload)) {
                payload = generated_payload;
                length = (size_t)written;
            }
        }

        if (payload == NULL || length == 0U) {
            continue;
        }

        // Delegate to WebSocket module for broadcasting
        web_server_websocket_broadcast_event(event.id, payload, length);
    }

    ESP_LOGI(TAG, "Event task shutting down cleanly");
    s_event_task_handle = NULL;

    // Notify parent task that we're done
    if (parent_task != NULL) {
        xTaskNotifyGive(parent_task);
    }

    vTaskDelete(NULL);
}
