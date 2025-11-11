#include "web_server.h"

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

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_events.h"
#include "config_manager.h"
#include "monitoring.h"
#include "mqtt_gateway.h"
#include "mqtt_client.h"
#include "history_logger.h"
#include "history_fs.h"
#include "alert_manager.h"
#include "web_server_alerts.h"
#include "can_victron.h"
#include "system_metrics.h"
#include "ota_update.h"
#include "system_control.h"
#include "web_server_ota_errors.h"

#include "cJSON.h"

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

// WebSocket rate limiting and security
#define WEB_SERVER_WS_MAX_PAYLOAD_SIZE    (32 * 1024)  // 32KB max payload
#define WEB_SERVER_WS_MAX_MSGS_PER_SEC    10           // Max 10 messages/sec per client
#define WEB_SERVER_WS_RATE_WINDOW_MS      1000         // 1 second rate limiting window

typedef struct ws_client {
    int fd;
    struct ws_client *next;
    // Rate limiting
    int64_t last_reset_time;      // Timestamp (ms) of rate window start
    uint32_t message_count;        // Messages sent in current window
    uint32_t total_violations;     // Total rate limit violations
} ws_client_t;

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
static ws_client_t *s_telemetry_clients = NULL;
static ws_client_t *s_event_clients = NULL;
static ws_client_t *s_uart_clients = NULL;
static ws_client_t *s_can_clients = NULL;
static ws_client_t *s_alert_clients = NULL;
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

static void web_server_set_http_status_code(httpd_req_t *req, int status_code)
{
    if (req == NULL) {
        return;
    }

    const char *status = "200 OK";
    switch (status_code) {
    case 200:
        status = "200 OK";
        break;
    case 400:
        status = "400 Bad Request";
        break;
    case 413:
        status = "413 Payload Too Large";
        break;
    case 415:
        status = "415 Unsupported Media Type";
        break;
    case 503:
        status = "503 Service Unavailable";
        break;
    default:
        status = "500 Internal Server Error";
        break;
    }

    httpd_resp_set_status(req, status);
}

static esp_err_t web_server_send_ota_response(httpd_req_t *req,
                                              web_server_ota_error_code_t code,
                                              const char *message_override,
                                              cJSON *data)
{
    if (req == NULL) {
        if (data != NULL) {
            cJSON_Delete(data);
        }
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        if (data != NULL) {
            cJSON_Delete(data);
        }
        return ESP_ERR_NO_MEM;
    }

    if (!web_server_ota_set_response_fields(root, code, message_override)) {
        cJSON_Delete(root);
        if (data != NULL) {
            cJSON_Delete(data);
        }
        return ESP_ERR_NO_MEM;
    }

    if (data != NULL) {
        cJSON_AddItemToObject(root, "data", data);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t length = strlen(json);
    web_server_set_http_status_code(req, web_server_ota_http_status(code));
    esp_err_t err = web_server_send_json(req, json, length);
    cJSON_free(json);
    return err;
}

static const uint8_t *web_server_memmem(const uint8_t *haystack,
                                        size_t haystack_len,
                                        const uint8_t *needle,
                                        size_t needle_len)
{
    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }

    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }

    return NULL;
}

typedef struct {
    char field_name[32];
    char filename[64];
    char content_type[64];
} web_server_multipart_headers_t;

static esp_err_t web_server_extract_boundary(const char *content_type,
                                             char *boundary,
                                             size_t boundary_size)
{
    if (content_type == NULL || boundary == NULL || boundary_size < 4U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(content_type, "multipart/form-data") == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *needle = "boundary=";
    const char *position = strstr(content_type, needle);
    if (position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    position += strlen(needle);
    if (*position == '\"') {
        ++position;
    }

    const char *end = position;
    while (*end != '\0' && *end != ';' && *end != ' ' && *end != '\"') {
        ++end;
    }

    size_t boundary_value_len = (size_t)(end - position);
    if (boundary_value_len == 0 || boundary_value_len + 2U >= boundary_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = snprintf(boundary, boundary_size, "--%.*s", (int)boundary_value_len, position);
    if (written < 0 || (size_t)written >= boundary_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static ssize_t web_server_parse_multipart_headers(uint8_t *buffer,
                                                  size_t buffer_len,
                                                  const char *boundary_line,
                                                  web_server_multipart_headers_t *out_headers)
{
    if (buffer == NULL || boundary_line == NULL || out_headers == NULL) {
        return -2;
    }

    const size_t boundary_len = strlen(boundary_line);
    if (buffer_len < boundary_len + 2U) {
        return -1;
    }

    if (memcmp(buffer, boundary_line, boundary_len) != 0) {
        return -2;
    }

    const uint8_t *cursor = buffer + boundary_len;
    const uint8_t *buffer_end = buffer + buffer_len;

    if (cursor + 2 > buffer_end || cursor[0] != '\r' || cursor[1] != '\n') {
        return -1;
    }
    cursor += 2;

    bool has_disposition = false;
    memset(out_headers, 0, sizeof(*out_headers));

    while (cursor < buffer_end) {
        const uint8_t *line_end = web_server_memmem(cursor, (size_t)(buffer_end - cursor), (const uint8_t *)"\r\n", 2);
        if (line_end == NULL) {
            return -1;
        }

        size_t line_length = (size_t)(line_end - cursor);
        if (line_length == 0) {
            cursor = line_end + 2;
            break;
        }

        if (line_length >= WEB_SERVER_MULTIPART_HEADER_MAX) {
            return -2;
        }

        char line[WEB_SERVER_MULTIPART_HEADER_MAX];
        memcpy(line, cursor, line_length);
        line[line_length] = '\0';

        if (strncasecmp(line, "Content-Disposition:", 20) == 0) {
            const char *name_token = strstr(line, "name=");
            if (name_token != NULL) {
                name_token += 5;
                if (*name_token == '\"') {
                    ++name_token;
                    const char *name_end = strchr(name_token, '\"');
                    if (name_end != NULL) {
                        size_t name_len = (size_t)(name_end - name_token);
                        if (name_len < sizeof(out_headers->field_name)) {
                            memcpy(out_headers->field_name, name_token, name_len);
                            out_headers->field_name[name_len] = '\0';
                        }
                    }
                }
            }

            const char *filename_token = strstr(line, "filename=");
            if (filename_token != NULL) {
                filename_token += 9;
                if (*filename_token == '\"') {
                    ++filename_token;
                    const char *filename_end = strchr(filename_token, '\"');
                    if (filename_end != NULL) {
                        size_t filename_len = (size_t)(filename_end - filename_token);
                        if (filename_len < sizeof(out_headers->filename)) {
                            memcpy(out_headers->filename, filename_token, filename_len);
                            out_headers->filename[filename_len] = '\0';
                        }
                    }
                }
            }

            has_disposition = true;
        } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            const char *value = line + 13;
            while (*value == ' ' || *value == '\t') {
                ++value;
            }
            size_t len = strnlen(value, sizeof(out_headers->content_type) - 1U);
            memcpy(out_headers->content_type, value, len);
            out_headers->content_type[len] = '\0';
        }

        cursor = line_end + 2;
    }

    if (!has_disposition) {
        return -2;
    }

    return (ssize_t)(cursor - buffer);
}

static esp_err_t web_server_process_multipart_body(uint8_t *buffer,
                                                   size_t *buffer_len,
                                                   const char *boundary_marker,
                                                   ota_update_session_t *session,
                                                   size_t *total_written,
                                                   bool *complete)
{
    if (buffer == NULL || buffer_len == NULL || boundary_marker == NULL || session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t marker_len = strlen(boundary_marker);
    const size_t guard = marker_len + 8U;
    size_t processed = 0;

    while (processed < *buffer_len) {
        size_t available = *buffer_len - processed;
        if (available == 0) {
            break;
        }

        const uint8_t *marker = web_server_memmem(buffer + processed, available,
                                                  (const uint8_t *)boundary_marker, marker_len);
        if (marker == NULL) {
            if (available <= guard) {
                break;
            }

            size_t chunk = available - guard;
            if (chunk > 0) {
                esp_err_t err = ota_update_write(session, buffer + processed, chunk);
                if (err != ESP_OK) {
                    return err;
                }
                if (total_written != NULL) {
                    *total_written += chunk;
                }
                processed += chunk;
                continue;
            }
            break;
        }

        size_t marker_index = (size_t)(marker - buffer);
        if (marker_index > processed) {
            size_t chunk = marker_index - processed;
            esp_err_t err = ota_update_write(session, buffer + processed, chunk);
            if (err != ESP_OK) {
                return err;
            }
            if (total_written != NULL) {
                *total_written += chunk;
            }
        }

        size_t after_marker = marker_index + marker_len;
        bool final = false;
        if (*buffer_len - after_marker >= 2 && memcmp(buffer + after_marker, "--", 2) == 0) {
            final = true;
            after_marker += 2;
        }
        if (*buffer_len - after_marker >= 2 && memcmp(buffer + after_marker, "\r\n", 2) == 0) {
            after_marker += 2;
        }

        processed = after_marker;
        if (complete != NULL) {
            *complete = final;
        }

        if (!final) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        break;
    }

    if (processed > 0) {
        size_t remaining = *buffer_len - processed;
        if (remaining > 0) {
            memmove(buffer, buffer + processed, remaining);
        }
        *buffer_len = remaining;
    }

    return ESP_OK;
}

static esp_err_t web_server_stream_firmware_upload(httpd_req_t *req,
                                                   ota_update_session_t *session,
                                                   const char *boundary_line,
                                                   web_server_multipart_headers_t *headers,
                                                   size_t *out_written)
{
    if (req == NULL || session == NULL || boundary_line == NULL || headers == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[WEB_SERVER_MULTIPART_BUFFER_SIZE];
    size_t buffer_len = 0U;
    size_t received = 0U;
    bool headers_parsed = false;
    bool upload_complete = false;
    size_t total_written = 0U;

    char boundary_marker[WEB_SERVER_MULTIPART_BOUNDARY_MAX + 4];
    int marker_written = snprintf(boundary_marker,
                                  sizeof(boundary_marker),
                                  "\r\n%s",
                                  boundary_line);
    if (marker_written < 0 || (size_t)marker_written >= sizeof(boundary_marker)) {
        return ESP_ERR_INVALID_SIZE;
    }

    while (!upload_complete || buffer_len > 0U || received < (size_t)req->content_len) {
        if (received < (size_t)req->content_len) {
            if (buffer_len >= sizeof(buffer)) {
                return ESP_ERR_INVALID_SIZE;
            }
            size_t to_read = sizeof(buffer) - buffer_len;
            int ret = httpd_req_recv(req, (char *)buffer + buffer_len, to_read);
            if (ret < 0) {
                if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;
                }
                return ESP_FAIL;
            }
            if (ret == 0) {
                break;
            }
            buffer_len += (size_t)ret;
            received += (size_t)ret;
        }

        if (!headers_parsed) {
            ssize_t header_end = web_server_parse_multipart_headers(buffer, buffer_len, boundary_line, headers);
            if (header_end == -1) {
                continue;
            }
            if (header_end < 0) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            size_t data_len = buffer_len - (size_t)header_end;
            if (data_len > 0) {
                memmove(buffer, buffer + header_end, data_len);
            }
            buffer_len = data_len;
            headers_parsed = true;
        }

        if (headers_parsed) {
            esp_err_t err = web_server_process_multipart_body(buffer,
                                                              &buffer_len,
                                                              boundary_marker,
                                                              session,
                                                              &total_written,
                                                              &upload_complete);
            if (err == ESP_ERR_INVALID_RESPONSE) {
                return err;
            }
            if (err != ESP_OK) {
                return err;
            }
        }

        if (upload_complete && buffer_len == 0U && received >= (size_t)req->content_len) {
            break;
        }
    }

    if (!upload_complete) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (out_written != NULL) {
        *out_written = total_written;
    }

    return ESP_OK;
}

static esp_err_t web_server_api_metrics_runtime_handler(httpd_req_t *req)
{
    system_metrics_runtime_t runtime;
    esp_err_t err = system_metrics_collect_runtime(&runtime);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to collect runtime metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Runtime metrics unavailable");
        return err;
    }

    char *buffer = malloc(WEB_SERVER_RUNTIME_JSON_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Memory allocation failure");
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    err = system_metrics_runtime_to_json(&runtime, buffer, WEB_SERVER_RUNTIME_JSON_SIZE, &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize runtime metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Runtime metrics serialization error");
        free(buffer);
        return err;
    }

    esp_err_t send_err = web_server_send_json(req, buffer, length);
    free(buffer);
    return send_err;
}

static esp_err_t web_server_api_event_bus_metrics_handler(httpd_req_t *req)
{
    system_metrics_event_bus_metrics_t metrics;
    esp_err_t err = system_metrics_collect_event_bus(&metrics);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to collect event bus metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Event bus metrics unavailable");
        return err;
    }

    char *buffer = malloc(WEB_SERVER_EVENT_BUS_JSON_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Memory allocation failure");
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    err = system_metrics_event_bus_to_json(&metrics, buffer, WEB_SERVER_EVENT_BUS_JSON_SIZE, &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize event bus metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Event bus metrics serialization error");
        free(buffer);
        return err;
    }

    esp_err_t send_err = web_server_send_json(req, buffer, length);
    free(buffer);
    return send_err;
}

static esp_err_t web_server_api_system_tasks_handler(httpd_req_t *req)
{
    system_metrics_task_snapshot_t tasks;
    esp_err_t err = system_metrics_collect_tasks(&tasks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to collect task metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task metrics unavailable");
        return err;
    }

    char *buffer = malloc(WEB_SERVER_TASKS_JSON_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Memory allocation failure");
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    err = system_metrics_tasks_to_json(&tasks, buffer, WEB_SERVER_TASKS_JSON_SIZE, &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize task metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task metrics serialization error");
        free(buffer);
        return err;
    }

    esp_err_t send_err = web_server_send_json(req, buffer, length);
    free(buffer);
    return send_err;
}

static esp_err_t web_server_api_system_modules_handler(httpd_req_t *req)
{
    system_metrics_event_bus_metrics_t event_bus_metrics;
    esp_err_t err = system_metrics_collect_event_bus(&event_bus_metrics);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to collect event bus metrics for modules: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Module metrics unavailable");
        return err;
    }

    system_metrics_module_snapshot_t modules;
    err = system_metrics_collect_modules(&modules, &event_bus_metrics);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to aggregate module metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Module metrics unavailable");
        return err;
    }

    char *buffer = malloc(WEB_SERVER_MODULES_JSON_SIZE);
    if (buffer == NULL) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Memory allocation failure");
        return ESP_ERR_NO_MEM;
    }

    size_t length = 0;
    err = system_metrics_modules_to_json(&modules, buffer, WEB_SERVER_MODULES_JSON_SIZE, &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize module metrics: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Module metrics serialization error");
        free(buffer);
        return err;
    }

    esp_err_t send_err = web_server_send_json(req, buffer, length);
    free(buffer);
    return send_err;
}

static void ws_client_list_add(ws_client_t **list, int fd)
{
    if (list == NULL || fd < 0 || s_ws_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (ws_client_t *iter = *list; iter != NULL; iter = iter->next) {
        if (iter->fd == fd) {
            xSemaphoreGive(s_ws_mutex);
            return;
        }
    }

    ws_client_t *client = calloc(1, sizeof(ws_client_t));
    if (client == NULL) {
        xSemaphoreGive(s_ws_mutex);
        ESP_LOGW(TAG, "Unable to allocate memory for websocket client");
        return;
    }

    client->fd = fd;
    client->next = *list;
    // Initialize rate limiting (calloc already zeroed message_count and total_violations)
    client->last_reset_time = esp_timer_get_time() / 1000;  // Convert to ms
    *list = client;

    xSemaphoreGive(s_ws_mutex);
}

static void ws_client_list_remove(ws_client_t **list, int fd)
{
    if (list == NULL || s_ws_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
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

    xSemaphoreGive(s_ws_mutex);
}

static void ws_client_list_broadcast(ws_client_t **list, const char *payload, size_t length)
{
    if (list == NULL || payload == NULL || length == 0 || s_ws_mutex == NULL || s_httpd == NULL) {
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

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
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

    xSemaphoreGive(s_ws_mutex);

    // Diffuser hors section critique pour éviter les blocages
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = payload_length,
    };

    for (size_t i = 0; i < client_count; i++) {
        esp_err_t err = httpd_ws_send_frame_async(s_httpd, client_fds[i], &frame);
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

static esp_err_t web_server_mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_SERVER_FS_BASE_PATH,
        .partition_label = NULL,
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "SPIFFS already mounted");
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(conf.partition_label, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
    }

    return ESP_OK;
}

static const char *web_server_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "text/plain";
    }

    if (strcasecmp(ext, ".html") == 0) {
        return "text/html";
    }
    if (strcasecmp(ext, ".js") == 0) {
        return "application/javascript";
    }
    if (strcasecmp(ext, ".css") == 0) {
        return "text/css";
    }
    if (strcasecmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (strcasecmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcasecmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

/**
 * Check if URI is secure (no path traversal attempts)
 * @param uri URI to check
 * @return true if secure, false otherwise
 *
 * Checks for:
 * - ../ and ..\\ sequences (including URL encoded variants)
 * - Absolute paths
 * - Null bytes
 * - Excessive path length
 */
static bool web_server_uri_is_secure(const char *uri)
{
    if (uri == NULL) {
        return false;
    }

    size_t len = strlen(uri);

    // Check for excessive length
    if (len == 0 || len > 256) {
        return false;
    }

    // Check for null bytes (truncation attacks)
    if (memchr(uri, '\0', len) != (uri + len)) {
        return false;
    }

    // Check for absolute paths (should be relative)
    if (uri[0] == '/') {
        // Allow single leading slash for root-relative paths
        if (len > 1 && uri[1] == '/') {
            return false; // Double slash not allowed
        }
    }

    // Check for various path traversal patterns
    const char *dangerous_patterns[] = {
        "../",      // Standard traversal
        "..\\",     // Windows style
        "%2e%2e/",  // URL encoded ../ (lowercase)
        "%2E%2E/",  // URL encoded ../ (uppercase)
        "%2e%2e\\", // URL encoded ..\ (lowercase)
        "%2E%2E\\", // URL encoded ..\ (uppercase)
        "..%2f",    // Partial encoding
        "..%2F",    // Partial encoding
        "..%5c",    // Partial encoding backslash
        "..%5C",    // Partial encoding backslash
        "%252e",    // Double URL encoded
        "....//",   // Obfuscated traversal
        NULL
    };

    for (int i = 0; dangerous_patterns[i] != NULL; i++) {
        if (strcasestr(uri, dangerous_patterns[i]) != NULL) {
            ESP_LOGW(TAG, "Path traversal attempt detected: %s", uri);
            return false;
        }
    }

    // Check for repeated slashes (path normalization bypass)
    for (size_t i = 0; i < len - 1; i++) {
        if (uri[i] == '/' && uri[i + 1] == '/') {
            return false;
        }
    }

    return true;
}

static esp_err_t web_server_send_file(httpd_req_t *req, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "Failed to open %s: %s", path, strerror(errno));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    web_server_set_security_headers(req);
    httpd_resp_set_type(req, web_server_content_type(path));
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=60, public");

    char buffer[WEB_SERVER_FILE_BUFSZ];
    ssize_t read_bytes = 0;
    do {
        read_bytes = read(fd, buffer, sizeof(buffer));
        if (read_bytes < 0) {
            ESP_LOGE(TAG, "Error reading %s: %s", path, strerror(errno));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read error");
            close(fd);
            return ESP_FAIL;
        }

        if (read_bytes > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, buffer, read_bytes);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send chunk for %s: %s", path, esp_err_to_name(err));
                close(fd);
                return err;
            }
        }
    } while (read_bytes > 0);

    httpd_resp_send_chunk(req, NULL, 0);
    close(fd);
    return ESP_OK;
}

static void web_server_parse_mqtt_uri(const char *uri,
                                      char *scheme,
                                      size_t scheme_size,
                                      char *host,
                                      size_t host_size,
                                      uint16_t *port_out)
{
    if (scheme != NULL && scheme_size > 0) {
        scheme[0] = '\0';
    }

}

static bool web_server_query_value_truthy(const char *value, size_t length)
{
    if (value == NULL || length == 0U) {
        return true;
    }

    if (length == 1U) {
        char c = (char)tolower((unsigned char)value[0]);
        return (c == '1') || (c == 'y') || (c == 't');
    }

    if (length == 2U && strncasecmp(value, "on", 2) == 0) {
        return true;
    }
    if (length == 3U && strncasecmp(value, "yes", 3) == 0) {
        return true;
    }
    if (length == 4U && strncasecmp(value, "true", 4) == 0) {
        return true;
    }

    return false;
}

bool web_server_uri_requests_full_snapshot(const char *uri)
{
    if (uri == NULL) {
        return false;
    }

    const char *query = strchr(uri, '?');
    if (query == NULL || *(++query) == '\0') {
        return false;
    }

    while (*query != '\0') {
        const char *next = strpbrk(query, "&;");
        size_t length = (next != NULL) ? (size_t)(next - query) : strlen(query);
        if (length > 0U) {
            const char *eq = memchr(query, '=', length);
            size_t key_len = (eq != NULL) ? (size_t)(eq - query) : length;
            if (key_len == sizeof("include_secrets") - 1U &&
                strncmp(query, "include_secrets", key_len) == 0) {
                if (eq == NULL) {
                    return true;
                }

                size_t value_len = length - key_len - 1U;
                const char *value = eq + 1;
                return web_server_query_value_truthy(value, value_len);
            }
        }

        if (next == NULL) {
            break;
        }
        query = next + 1;
    }

    return false;
}

static const char *web_server_mqtt_event_to_string(mqtt_client_event_id_t id)
{
    switch (id) {
        case MQTT_CLIENT_EVENT_CONNECTED:
            return "connected";
        case MQTT_CLIENT_EVENT_DISCONNECTED:
            return "disconnected";
        case MQTT_CLIENT_EVENT_SUBSCRIBED:
            return "subscribed";
        case MQTT_CLIENT_EVENT_PUBLISHED:
            return "published";
        case MQTT_CLIENT_EVENT_DATA:
            return "data";
        case MQTT_CLIENT_EVENT_ERROR:
            return "error";
        default:
            return "unknown";
    }
}


    if (host != NULL && host_size > 0) {
        host[0] = '\0';
    }
    if (port_out != NULL) {
        *port_out = 1883U;
    }

    if (uri == NULL) {
        if (scheme != NULL && scheme_size > 0) {
            (void)snprintf(scheme, scheme_size, "%s", "mqtt");
        }
        return;
    }

    const char *authority = uri;
    const char *sep = strstr(uri, "://");
    char scheme_buffer[16] = "mqtt";
    if (sep != NULL) {
        size_t len = (size_t)(sep - uri);
        if (len >= sizeof(scheme_buffer)) {
            len = sizeof(scheme_buffer) - 1U;
        }
        memcpy(scheme_buffer, uri, len);
        scheme_buffer[len] = '\0';
        authority = sep + 3;
    }

    for (size_t i = 0; scheme_buffer[i] != '\0'; ++i) {
        scheme_buffer[i] = (char)tolower((unsigned char)scheme_buffer[i]);
    }
    if (scheme != NULL && scheme_size > 0) {
        (void)snprintf(scheme, scheme_size, "%s", scheme_buffer);
    }

    uint16_t port = (strcmp(scheme_buffer, "mqtts") == 0) ? 8883U : 1883U;
    if (authority == NULL || authority[0] == '\0') {
        if (port_out != NULL) {
            *port_out = port;
        }
        return;
    }

    const char *path = strpbrk(authority, "/?");
    size_t length = (path != NULL) ? (size_t)(path - authority) : strlen(authority);
    if (length == 0) {
        if (port_out != NULL) {
            *port_out = port;
        }
        return;
    }

    char host_buffer[MQTT_CLIENT_MAX_URI_LENGTH];
    if (length >= sizeof(host_buffer)) {
        length = sizeof(host_buffer) - 1U;
    }
    memcpy(host_buffer, authority, length);
    host_buffer[length] = '\0';

    char *colon = strrchr(host_buffer, ':');
    if (colon != NULL) {
        *colon = '\0';
        ++colon;
        char *endptr = NULL;
        unsigned long parsed = strtoul(colon, &endptr, 10);
        if (endptr != colon && parsed <= UINT16_MAX) {
            port = (uint16_t)parsed;
        }
    }

    if (host != NULL && host_size > 0) {
        (void)snprintf(host, host_size, "%s", host_buffer);
    }
    if (port_out != NULL) {
        *port_out = port;
    }
}

static esp_err_t web_server_static_get_handler(httpd_req_t *req)
{
    char filepath[WEB_SERVER_MAX_PATH];
    const char *uri = req->uri;

    if (!web_server_uri_is_secure(uri)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    if (strcmp(uri, "/") == 0) {
        uri = WEB_SERVER_INDEX_PATH + strlen(WEB_SERVER_WEB_ROOT);
    }

    int written = snprintf(filepath, sizeof(filepath), "%s%s", WEB_SERVER_WEB_ROOT, uri);
    if (written <= 0 || written >= (int)sizeof(filepath)) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Path too long");
        return ESP_FAIL;
    }

    struct stat st = {0};
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "Static asset not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    return web_server_send_file(req, filepath);
}

static esp_err_t web_server_api_status_handler(httpd_req_t *req)
{
    char snapshot[MONITORING_SNAPSHOT_MAX_SIZE];
    size_t length = 0;
    esp_err_t err = monitoring_get_status_json(snapshot, sizeof(snapshot), &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build status JSON: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status unavailable");
        return err;
    }

    if (length >= sizeof(snapshot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status too large");
        return ESP_ERR_INVALID_SIZE;
    }

    snapshot[length] = '\0';

    char response[MONITORING_SNAPSHOT_MAX_SIZE + 32U];
    int written = snprintf(response, sizeof(response), "{\"battery\":%s}", snapshot);
    if (written <= 0 || (size_t)written >= sizeof(response)) {
        ESP_LOGE(TAG, "Failed to wrap status response");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status unavailable");
        return ESP_ERR_INVALID_SIZE;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, written);
}

static bool web_server_request_authorized_for_secrets(httpd_req_t *req)
{
    if (s_config_secret_authorizer == NULL) {
        return false;
    }
    return s_config_secret_authorizer(req);
}

esp_err_t web_server_prepare_config_snapshot(const char *uri,
                                             bool authorized_for_secrets,
                                             char *buffer,
                                             size_t buffer_size,
                                             size_t *out_length,
                                             const char **visibility_out)
{
    if (visibility_out != NULL) {
        *visibility_out = NULL;
    }

    bool wants_secrets = web_server_uri_requests_full_snapshot(uri);
    config_manager_snapshot_flags_t flags = CONFIG_MANAGER_SNAPSHOT_PUBLIC;
    const char *visibility = "public";

    if (wants_secrets) {
        if (authorized_for_secrets) {
            flags = CONFIG_MANAGER_SNAPSHOT_INCLUDE_SECRETS;
            visibility = "full";
        } else {
            ESP_LOGW(TAG, "Client requested config secrets without authorization");
        }
    }

    esp_err_t err = config_manager_get_config_json(buffer, buffer_size, out_length, flags);
    if (err == ESP_OK && visibility_out != NULL) {
        *visibility_out = visibility;
    }
    return err;
}

static esp_err_t web_server_api_config_get_handler(httpd_req_t *req)
{
    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t length = 0;
    const char *visibility = NULL;
    bool authorized = web_server_request_authorized_for_secrets(req);
    esp_err_t err = web_server_prepare_config_snapshot(req->uri,
                                                       authorized,
                                                       buffer,
                                                       sizeof(buffer),
                                                       &length,
                                                       &visibility);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load configuration JSON: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config unavailable");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (visibility != NULL) {
        httpd_resp_set_hdr(req, "X-Config-Snapshot", visibility);
    }
    return httpd_resp_send(req, buffer, length);
}

static esp_err_t web_server_api_config_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_ERR_INVALID_SIZE;
    }

    if (req->content_len + 1 > CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        httpd_resp_send_err(req, HTTPD_413_PAYLOAD_TOO_LARGE, "Config too large");
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Error receiving config payload: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read error");
            return ESP_FAIL;
        }
        received += ret;
    }

    buffer[received] = '\0';

    esp_err_t err = config_manager_set_config_json(buffer, received);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"updated\"}");
}

static esp_err_t web_server_api_mqtt_config_get_handler(httpd_req_t *req)
{
    const mqtt_client_config_t *config = config_manager_get_mqtt_client_config();
    const config_manager_mqtt_topics_t *topics = config_manager_get_mqtt_topics();
    if (config == NULL || topics == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "MQTT config unavailable");
        return ESP_FAIL;
    }

    char scheme[16];
    char host[MQTT_CLIENT_MAX_URI_LENGTH];
    uint16_t port = 0U;
    web_server_parse_mqtt_uri(config->broker_uri, scheme, sizeof(scheme), host, sizeof(host), &port);

    // Mask password for security - never send actual password in GET response
    const char *masked_password = config_manager_mask_secret(config->password);

    char buffer[WEB_SERVER_MQTT_JSON_SIZE];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"scheme\":\"%s\",\"broker_uri\":\"%s\",\"host\":\"%s\",\"port\":%u,"
                           "\"username\":\"%s\",\"password\":\"%s\",\"client_cert_path\":\"%s\","
                           "\"ca_cert_path\":\"%s\",\"verify_hostname\":%s,\"keepalive\":%u,\"default_qos\":%u,"
                           "\"retain\":%s,\"topics\":{\"status\":\"%s\",\"metrics\":\"%s\",\"config\":\"%s\","
                           "\"can_raw\":\"%s\",\"can_decoded\":\"%s\",\"can_ready\":\"%s\"}}",
                           scheme,
                           config->broker_uri,
                           host,
                           (unsigned)port,
                           config->username,
                           masked_password,
                           config->client_cert_path,
                           config->ca_cert_path,
                           config->verify_hostname ? "true" : "false",
                           (unsigned)config->keepalive_seconds,
                           (unsigned)config->default_qos,
                           config->retain_enabled ? "true" : "false",
                           topics->status,
                           topics->metrics,
                           topics->config,
                           topics->can_raw,
                           topics->can_decoded,
                           topics->can_ready);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "MQTT config too large");
        return ESP_ERR_INVALID_SIZE;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buffer, written);
}

static esp_err_t web_server_api_mqtt_config_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_ERR_INVALID_SIZE;
    }

    if (req->content_len + 1 >= CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        httpd_resp_send_err(req, HTTPD_413_PAYLOAD_TOO_LARGE, "Payload too large");
        return ESP_ERR_INVALID_SIZE;
    }

    char payload[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int ret = httpd_req_recv(req, payload + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read error");
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    payload[received] = '\0';

    const mqtt_client_config_t *current = config_manager_get_mqtt_client_config();
    const config_manager_mqtt_topics_t *current_topics = config_manager_get_mqtt_topics();
    if (current == NULL || current_topics == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "MQTT config unavailable");
        return ESP_FAIL;
    }

    mqtt_client_config_t updated = *current;
    config_manager_mqtt_topics_t topics = *current_topics;

    char default_scheme[16];
    char default_host[MQTT_CLIENT_MAX_URI_LENGTH];
    uint16_t default_port = 0U;
    web_server_parse_mqtt_uri(updated.broker_uri,
                              default_scheme,
                              sizeof(default_scheme),
                              default_host,
                              sizeof(default_host),
                              &default_port);

    char scheme[sizeof(default_scheme)];
    snprintf(scheme, sizeof(scheme), "%s", default_scheme);
    char host[sizeof(default_host)];
    snprintf(host, sizeof(host), "%s", default_host);
    uint16_t port = default_port;

    esp_err_t status = ESP_OK;
    bool send_error = false;
    int error_status = HTTPD_400_BAD_REQUEST;
    const char *error_message = "Invalid MQTT configuration";

    cJSON *root = cJSON_ParseWithLength(payload, received);
    if (root == NULL || !cJSON_IsObject(root)) {
        status = ESP_ERR_INVALID_ARG;
        send_error = true;
        error_message = "Invalid JSON payload";
        goto cleanup;
    }

    const cJSON *item = NULL;

    item = cJSON_GetObjectItemCaseSensitive(root, "scheme");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "scheme must be a string";
            goto cleanup;
        }
        snprintf(scheme, sizeof(scheme), "%s", item->valuestring);
        for (size_t i = 0; scheme[i] != '\0'; ++i) {
            scheme[i] = (char)tolower((unsigned char)scheme[i]);
        }
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "host");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "host must be a string";
            goto cleanup;
        }
        snprintf(host, sizeof(host), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "port");
    if (item != NULL) {
        if (!cJSON_IsNumber(item)) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "port must be a number";
            goto cleanup;
        }
        double value = item->valuedouble;
        if ((double)item->valueint != value || value < 1.0 || value > UINT16_MAX) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "Invalid port";
            goto cleanup;
        }
        port = (uint16_t)item->valueint;
    }

    if (host[0] == '\0') {
        status = ESP_ERR_INVALID_ARG;
        send_error = true;
        error_message = "Host is required";
        goto cleanup;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "username");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "username must be a string";
            goto cleanup;
        }
        snprintf(updated.username, sizeof(updated.username), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "password must be a string";
            goto cleanup;
        }
        snprintf(updated.password, sizeof(updated.password), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "client_cert_path");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "client_cert_path must be a string";
            goto cleanup;
        }
        snprintf(updated.client_cert_path, sizeof(updated.client_cert_path), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "ca_cert_path");
    if (item != NULL) {
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "ca_cert_path must be a string";
            goto cleanup;
        }
        snprintf(updated.ca_cert_path, sizeof(updated.ca_cert_path), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "verify_hostname");
    if (item != NULL) {
        if (!cJSON_IsBool(item)) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "verify_hostname must be a boolean";
            goto cleanup;
        }
        updated.verify_hostname = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "keepalive");
    if (item != NULL) {
        if (!cJSON_IsNumber(item) || item->valuedouble < 0.0) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "keepalive must be a non-negative number";
            goto cleanup;
        }
        if ((double)item->valueint != item->valuedouble || item->valueint < 0 || item->valueint > UINT16_MAX) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "Invalid keepalive";
            goto cleanup;
        }
        updated.keepalive_seconds = (uint16_t)item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "default_qos");
    if (item != NULL) {
        if (!cJSON_IsNumber(item)) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "default_qos must be a number";
            goto cleanup;
        }
        if ((double)item->valueint != item->valuedouble || item->valueint < 0 || item->valueint > 2) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "default_qos must be between 0 and 2";
            goto cleanup;
        }
        updated.default_qos = (uint8_t)item->valueint;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "retain");
    if (item != NULL) {
        if (!cJSON_IsBool(item)) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "retain must be a boolean";
            goto cleanup;
        }
        updated.retain_enabled = cJSON_IsTrue(item);
    }

    const cJSON *topics_obj = cJSON_GetObjectItemCaseSensitive(root, "topics");
    if (topics_obj != NULL) {
        if (!cJSON_IsObject(topics_obj)) {
            status = ESP_ERR_INVALID_ARG;
            send_error = true;
            error_message = "topics must be an object";
            goto cleanup;
        }

        const cJSON *topic_item = NULL;
        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "status");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.status must be a string";
                goto cleanup;
            }
            snprintf(topics.status, sizeof(topics.status), "%s", topic_item->valuestring);
        }

        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "metrics");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.metrics must be a string";
                goto cleanup;
            }
            snprintf(topics.metrics, sizeof(topics.metrics), "%s", topic_item->valuestring);
        }

        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "config");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.config must be a string";
                goto cleanup;
            }
            snprintf(topics.config, sizeof(topics.config), "%s", topic_item->valuestring);
        }

        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "can_raw");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.can_raw must be a string";
                goto cleanup;
            }
            snprintf(topics.can_raw, sizeof(topics.can_raw), "%s", topic_item->valuestring);
        }

        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "can_decoded");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.can_decoded must be a string";
                goto cleanup;
            }
            snprintf(topics.can_decoded, sizeof(topics.can_decoded), "%s", topic_item->valuestring);
        }

        topic_item = cJSON_GetObjectItemCaseSensitive(topics_obj, "can_ready");
        if (topic_item != NULL) {
            if (!cJSON_IsString(topic_item) || topic_item->valuestring == NULL) {
                status = ESP_ERR_INVALID_ARG;
                send_error = true;
                error_message = "topics.can_ready must be a string";
                goto cleanup;
            }
            snprintf(topics.can_ready, sizeof(topics.can_ready), "%s", topic_item->valuestring);
        }
    }

    int uri_len = snprintf(updated.broker_uri,
                           sizeof(updated.broker_uri),
                           "%s://%s:%u",
                           (scheme[0] != '\0') ? scheme : "mqtt",
                           host,
                           (unsigned)port);
    if (uri_len < 0 || uri_len >= (int)sizeof(updated.broker_uri)) {
        status = ESP_ERR_INVALID_ARG;
        send_error = true;
        error_message = "Broker URI too long";
        goto cleanup;
    }

    status = config_manager_set_mqtt_client_config(&updated);
    if (status != ESP_OK) {
        send_error = true;
        error_message = "Failed to update MQTT client";
        goto cleanup;
    }

    status = config_manager_set_mqtt_topics(&topics);
    if (status != ESP_OK) {
        send_error = true;
        error_message = "Failed to update MQTT topics";
        goto cleanup;
    }

    httpd_resp_set_type(req, "application/json");
    status = httpd_resp_sendstr(req, "{\\"status\\":\\"updated\\"}");
    goto cleanup;

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (send_error) {
        httpd_resp_send_err(req, error_status, error_message);
    }
    return status;

static esp_err_t web_server_api_ota_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_EMPTY_PAYLOAD, NULL, NULL);
    }

    char content_type[WEB_SERVER_MULTIPART_HEADER_MAX];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_MISSING_CONTENT_TYPE, NULL, NULL);
    }

    char boundary_line[WEB_SERVER_MULTIPART_BOUNDARY_MAX];
    esp_err_t err = web_server_extract_boundary(content_type, boundary_line, sizeof(boundary_line));
    if (err != ESP_OK) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY, NULL, NULL);
    }

    ota_update_session_t *session = NULL;
    err = ota_update_begin(&session, req->content_len);
    if (err != ESP_OK) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_SUBSYSTEM_BUSY, NULL, NULL);
    }

    web_server_multipart_headers_t headers;
    size_t bytes_written = 0U;
    err = web_server_stream_firmware_upload(req, session, boundary_line, &headers, &bytes_written);
    if (err != ESP_OK) {
        ota_update_abort(session);
        web_server_ota_error_code_t code = (err == ESP_ERR_INVALID_RESPONSE)
            ? WEB_SERVER_OTA_ERROR_MALFORMED_MULTIPART
            : WEB_SERVER_OTA_ERROR_STREAM_FAILURE;
        return web_server_send_ota_response(req, code, NULL, NULL);
    }

    (void)bytes_written;

    if (headers.field_name[0] == '\0' || strcmp(headers.field_name, "firmware") != 0) {
        ota_update_abort(session);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_MISSING_FIRMWARE_FIELD, NULL, NULL);
    }

    if (headers.content_type[0] != '\0' &&
        strncasecmp(headers.content_type, "application/octet-stream", sizeof(headers.content_type)) != 0 &&
        strncasecmp(headers.content_type, "application/x-binary", sizeof(headers.content_type)) != 0) {
        ota_update_abort(session);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE, NULL, NULL);
    }

    ota_update_result_t result = {0};
    err = ota_update_finalize(session, &result);
    if (err != ESP_OK) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_VALIDATION_FAILED, NULL, NULL);
    }

    if (s_event_publisher != NULL) {
        const char *filename = (headers.filename[0] != '\0') ? headers.filename : "firmware.bin";
        int label_written = snprintf(s_ota_event_label,
                                     sizeof(s_ota_event_label),
                                     "%s (%zu bytes, crc32=%08" PRIX32 " )",
                                     filename,
                                     result.bytes_written,
                                     result.crc32);
        if (label_written > 0 && (size_t)label_written < sizeof(s_ota_event_label)) {
#ifdef ESP_PLATFORM
            s_ota_event_metadata.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
            s_ota_event_metadata.timestamp_ms = 0U;
#endif
            event_bus_event_t event = {
                .id = APP_EVENT_ID_OTA_UPLOAD_READY,
                .payload = &s_ota_event_metadata,
                .payload_size = sizeof(s_ota_event_metadata),
            };
            s_event_publisher(&event, pdMS_TO_TICKS(50));
        }
    }

    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    if (cJSON_AddNumberToObject(data, "bytes", (double)result.bytes_written) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    char crc_buffer[9];
    snprintf(crc_buffer, sizeof(crc_buffer), "%08" PRIX32, result.crc32);
    if (cJSON_AddStringToObject(data, "crc32", crc_buffer) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    const char *partition = (result.partition_label[0] != '\0') ? result.partition_label : "unknown";
    if (cJSON_AddStringToObject(data, "partition", partition) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    const char *version = (result.new_version[0] != '\0') ? result.new_version : "unknown";
    if (cJSON_AddStringToObject(data, "version", version) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    if (cJSON_AddBoolToObject(data, "reboot_required", result.reboot_required) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    if (cJSON_AddBoolToObject(data, "version_changed", result.version_changed) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    const char *filename = (headers.filename[0] != '\0') ? headers.filename : "firmware.bin";
    if (cJSON_AddStringToObject(data, "filename", filename) == NULL) {
        cJSON_Delete(data);
        return web_server_send_ota_response(req, WEB_SERVER_OTA_ERROR_ENCODING_FAILED, NULL, NULL);
    }

    return web_server_send_ota_response(req, WEB_SERVER_OTA_OK, NULL, data);
}

static esp_err_t web_server_api_restart_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    size_t received = 0U;

    if ((size_t)req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_413_PAYLOAD_TOO_LARGE, "Restart payload too large");
        return ESP_ERR_INVALID_SIZE;
    }

    while (received < (size_t)req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret < 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read restart payload");
            return ESP_FAIL;
        }
        if (ret == 0) {
            break;
        }
        received += (size_t)ret;
    }

    char target_buf[16] = "bms";
    const char *target = target_buf;
    uint32_t delay_ms = WEB_SERVER_RESTART_DEFAULT_DELAY_MS;

    if (received > 0U) {
        body[received] = '\0';
        cJSON *json = cJSON_Parse(body);
        if (json == NULL) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON payload");
            return ESP_ERR_INVALID_ARG;
        }

        const cJSON *target_item = cJSON_GetObjectItemCaseSensitive(json, "target");
        if (cJSON_IsString(target_item) && target_item->valuestring != NULL) {
            strncpy(target_buf, target_item->valuestring, sizeof(target_buf) - 1U);
            target_buf[sizeof(target_buf) - 1U] = '\0';
        }

        const cJSON *delay_item = cJSON_GetObjectItemCaseSensitive(json, "delay_ms");
        if (cJSON_IsNumber(delay_item) && delay_item->valuedouble >= 0.0) {
            delay_ms = (uint32_t)delay_item->valuedouble;
        }

        cJSON_Delete(json);
    }

    bool request_gateway_restart = false;
    bool bms_attempted = false;
    const char *bms_status = "skipped";
    esp_err_t bms_err = ESP_OK;

    if (target != NULL && strcasecmp(target, "gateway") == 0) {
        request_gateway_restart = true;
    } else {
        bms_attempted = true;
        bms_err = system_control_request_bms_restart(0U);
        if (bms_err == ESP_OK) {
            bms_status = "ok";
        } else if (bms_err == ESP_ERR_INVALID_STATE) {
            bms_status = "throttled";
        } else if (bms_err == ESP_ERR_TIMEOUT) {
            bms_status = "timeout";
        } else {
            bms_status = esp_err_to_name(bms_err);
        }

        if (bms_err != ESP_OK) {
            request_gateway_restart = true;
        }
    }

    if (request_gateway_restart) {
        esp_err_t gw_err = system_control_schedule_gateway_restart(delay_ms);
        if (gw_err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to schedule gateway restart");
            return gw_err;
        }
    }

    if (s_event_publisher != NULL) {
        const char *mode = request_gateway_restart ? "gateway" : "bms";
        const char *suffix = (request_gateway_restart && bms_attempted && bms_err != ESP_OK) ? "+fallback" : "";
        int label_written = snprintf(s_restart_event_label,
                                     sizeof(s_restart_event_label),
                                     "Restart requested (%s%s)",
                                     mode,
                                     suffix);
        if (label_written > 0 && (size_t)label_written < sizeof(s_restart_event_label)) {
#ifdef ESP_PLATFORM
            s_restart_event_metadata.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
            s_restart_event_metadata.timestamp_ms = 0U;
#endif
            event_bus_event_t event = {
                .id = APP_EVENT_ID_UI_NOTIFICATION,
                .payload = &s_restart_event_metadata,
                .payload_size = sizeof(s_restart_event_metadata),
            };
            s_event_publisher(&event, pdMS_TO_TICKS(50));
        }
    }

    char response[256];
    int written = snprintf(response,
                           sizeof(response),
                           "{\"status\":\"scheduled\",\"bms_attempted\":%s,\"bms_status\":\"%s\",\"gateway_restart\":%s,\"delay_ms\":%u}",
                           bms_attempted ? "true" : "false",
                           bms_status,
                           request_gateway_restart ? "true" : "false",
                           request_gateway_restart ? delay_ms : 0U);
    if (written < 0 || (size_t)written >= sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Restart response too large");
        return ESP_ERR_INVALID_SIZE;
    }

    if (request_gateway_restart) {
        httpd_resp_set_status(req, "202 Accepted");
    }

    return web_server_send_json(req, response, (size_t)written);
}

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

static esp_err_t web_server_telemetry_ws_handler(httpd_req_t *req)
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

static esp_err_t web_server_events_ws_handler(httpd_req_t *req)
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

static esp_err_t web_server_uart_ws_handler(httpd_req_t *req)
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

static esp_err_t web_server_can_ws_handler(httpd_req_t *req)
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

static void web_server_event_task(void *context)
{
    TaskHandle_t parent_task = (TaskHandle_t)context;

    if (s_event_subscription == NULL) {
        s_event_task_handle = NULL;
        if (parent_task != NULL) {
            xTaskNotifyGive(parent_task);
        }
        vTaskDelete(NULL);
        return;
    }

    event_bus_event_t event = {0};
    while (!s_event_task_should_stop) {
        // Utiliser un timeout pour permettre la vérification périodique du drapeau de terminaison
        if (!event_bus_receive(s_event_subscription, &event, pdMS_TO_TICKS(1000))) {
            continue; // Timeout, vérifier le drapeau et réessayer
        }

        const char *payload = NULL;
        size_t length = 0U;
        char generated_payload[WEB_SERVER_EVENT_BUS_JSON_SIZE];

        if (event.payload != NULL && event.payload_size == sizeof(app_event_metadata_t)) {
            const app_event_metadata_t *metadata = (const app_event_metadata_t *)event.payload;
            if (metadata->event_id == event.id) {
                const char *key = (metadata->key != NULL) ? metadata->key : "";
                const char *type = (metadata->type != NULL) ? metadata->type : "";
                const char *label = (metadata->label != NULL) ? metadata->label : "";
                unsigned long long timestamp = (unsigned long long)metadata->timestamp_ms;
                int written = snprintf(generated_payload,
                                       sizeof(generated_payload),
                                       "{\"event_id\":%u,\"key\":\"%s\",\"type\":\"%s\",\"timestamp\":%llu",
                                       (unsigned)metadata->event_id,
                                       key,
                                       type,
                                       timestamp);
                if (written > 0 && (size_t)written < sizeof(generated_payload)) {
                    size_t used = (size_t)written;
                    if (label[0] != '\0' && used < sizeof(generated_payload)) {
                        int appended = snprintf(generated_payload + used,
                                                sizeof(generated_payload) - used,
                                                ",\"label\":\"%s\"",
                                                label);
                        if (appended > 0 && (size_t)appended < sizeof(generated_payload) - used) {
                            used += (size_t)appended;
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

        switch (event.id) {
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

    ESP_LOGI(TAG, "Event task shutting down cleanly");
    s_event_task_handle = NULL;

    // Notify parent task that we're done
    if (parent_task != NULL) {
        xTaskNotifyGive(parent_task);
    }

    vTaskDelete(NULL);
}

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

    // Free all WebSocket client lists
    if (s_ws_mutex != NULL) {
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        ws_client_list_free(&s_telemetry_clients);
        ws_client_list_free(&s_event_clients);
        ws_client_list_free(&s_uart_clients);
        ws_client_list_free(&s_can_clients);
        ws_client_list_free(&s_alert_clients);
        xSemaphoreGive(s_ws_mutex);
    }

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

    ESP_LOGI(TAG, "Web server deinitialized");
