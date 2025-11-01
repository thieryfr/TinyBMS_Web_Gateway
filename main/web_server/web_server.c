#include "web_server.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_events.h"
#include "config_manager.h"
#include "monitoring.h"
#include "mqtt_gateway.h"

#ifndef HTTPD_413_PAYLOAD_TOO_LARGE
#define HTTPD_413_PAYLOAD_TOO_LARGE 413
#endif

#ifndef HTTPD_414_URI_TOO_LONG
#define HTTPD_414_URI_TOO_LONG 414
#endif

#define WEB_SERVER_FS_BASE_PATH "/spiffs"
#define WEB_SERVER_WEB_ROOT     WEB_SERVER_FS_BASE_PATH
#define WEB_SERVER_INDEX_PATH   WEB_SERVER_WEB_ROOT "/index.html"
#define WEB_SERVER_MAX_PATH     256
#define WEB_SERVER_FILE_BUFSZ   1024
#define WEB_SERVER_HISTORY_JSON_SIZE 4096
#define WEB_SERVER_MQTT_JSON_SIZE    768

typedef struct ws_client {
    int fd;
    struct ws_client *next;
} ws_client_t;

static const char *TAG = "web_server";

static event_bus_publish_fn_t s_event_publisher = NULL;
static httpd_handle_t s_httpd = NULL;
static SemaphoreHandle_t s_ws_mutex = NULL;
static ws_client_t *s_telemetry_clients = NULL;
static ws_client_t *s_event_clients = NULL;
static ws_client_t *s_uart_clients = NULL;
static ws_client_t *s_can_clients = NULL;
static event_bus_subscription_handle_t s_event_subscription = NULL;
static TaskHandle_t s_event_task_handle = NULL;

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

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    ws_client_t *prev = NULL;
    ws_client_t *iter = *list;
    while (iter != NULL) {
        size_t payload_length = length;
        if (payload_length > 0 && payload[payload_length - 1] == '\0') {
            payload_length -= 1;
        }

        if (payload_length == 0) {
            prev = iter;
            iter = iter->next;
            continue;
        }

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)payload,
            .len = payload_length,
        };

        esp_err_t err = httpd_ws_send_frame_async(s_httpd, iter->fd, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Dropping websocket client %d: %s", iter->fd, esp_err_to_name(err));
            ws_client_t *to_free = iter;
            iter = iter->next;
            if (prev == NULL) {
                *list = iter;
            } else {
                prev->next = iter;
            }
            free(to_free);
            continue;
        }

        prev = iter;
        iter = iter->next;
    }

    xSemaphoreGive(s_ws_mutex);
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

static bool web_server_uri_is_secure(const char *uri)
{
    return uri != NULL && strstr(uri, "../") == NULL;
}

static esp_err_t web_server_send_file(httpd_req_t *req, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "Failed to open %s: %s", path, strerror(errno));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

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

static bool web_server_extract_json_string(const char *json, const char *field, char *out, size_t out_size)
{
    if (json == NULL || field == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor != '"') {
        return false;
    }
    ++cursor;

    size_t len = 0;
    while (cursor[len] != '\0' && cursor[len] != '"') {
        if (len + 1 >= out_size) {
            return false;
        }
        out[len] = cursor[len];
        ++len;
    }

    if (cursor[len] != '"') {
        return false;
    }

    out[len] = '\0';
    return true;
}

static bool web_server_extract_json_uint(const char *json, const char *field, uint32_t *out_value)
{
    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor == '\0') {
        return false;
    }

    char *endptr = NULL;
    unsigned long parsed = strtoul(cursor, &endptr, 10);
    if (endptr == cursor) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

static bool web_server_extract_json_bool(const char *json, const char *field, bool *out_value)
{
    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    const char *cursor = strstr(json, field);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return false;
    }

    ++cursor;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (strncmp(cursor, "true", 4) == 0) {
        *out_value = true;
        return true;
    }
    if (strncmp(cursor, "false", 5) == 0) {
        *out_value = false;
        return true;
    }

    return false;
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
    char buffer[256];
    size_t length = 0;
    esp_err_t err = monitoring_get_status_json(buffer, sizeof(buffer), &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build status JSON: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status unavailable");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buffer, length);
}

static esp_err_t web_server_api_config_get_handler(httpd_req_t *req)
{
    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t length = 0;
    esp_err_t err = config_manager_get_config_json(buffer, sizeof(buffer), &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load configuration JSON: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config unavailable");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
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
        if (ret < 0) {
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

    char buffer[WEB_SERVER_MQTT_JSON_SIZE];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"scheme\":\"%s\",\"broker_uri\":\"%s\",\"host\":\"%s\",\"port\":%u,"
                           "\"username\":\"%s\",\"password\":\"%s\",\"keepalive\":%u,\"default_qos\":%u,"
                           "\"retain\":%s,\"topics\":{\"status\":\"%s\",\"metrics\":\"%s\",\"config\":\"%s\"," 
                           "\"can_raw\":\"%s\",\"can_decoded\":\"%s\",\"can_ready\":\"%s\"}}",
                           scheme,
                           config->broker_uri,
                           host,
                           (unsigned)port,
                           config->username,
                           config->password,
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

    char scheme[16];
    (void)snprintf(scheme, sizeof(scheme), "%s", default_scheme);
    if (web_server_extract_json_string(payload, "\"scheme\"", scheme, sizeof(scheme))) {
        for (size_t i = 0; scheme[i] != '\0'; ++i) {
            scheme[i] = (char)tolower((unsigned char)scheme[i]);
        }
    }

    char host[MQTT_CLIENT_MAX_URI_LENGTH];
    (void)snprintf(host, sizeof(host), "%s", default_host);
    web_server_extract_json_string(payload, "\"host\"", host, sizeof(host));

    uint32_t port = default_port;
    web_server_extract_json_uint(payload, "\"port\"", &port);
    if (port == 0U || port > UINT16_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid port");
        return ESP_ERR_INVALID_ARG;
    }

    if (host[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Host is required");
        return ESP_ERR_INVALID_ARG;
    }

    web_server_extract_json_string(payload, "\"username\"", updated.username, sizeof(updated.username));
    web_server_extract_json_string(payload, "\"password\"", updated.password, sizeof(updated.password));

    uint32_t keepalive = 0U;
    if (web_server_extract_json_uint(payload, "\"keepalive\"", &keepalive)) {
        updated.keepalive_seconds = (uint16_t)keepalive;
    }

    uint32_t qos = 0U;
    if (web_server_extract_json_uint(payload, "\"default_qos\"", &qos)) {
        if (qos > 2U) {
            qos = 2U;
        }
        updated.default_qos = (uint8_t)qos;
    }

    bool retain_flag = false;
    if (web_server_extract_json_bool(payload, "\"retain\"", &retain_flag)) {
        updated.retain_enabled = retain_flag;
    }

    int uri_len = snprintf(updated.broker_uri,
                           sizeof(updated.broker_uri),
                           "%s://%s:%u",
                           (scheme[0] != '\0') ? scheme : "mqtt",
                           host,
                           (unsigned)port);
    if (uri_len < 0 || uri_len >= (int)sizeof(updated.broker_uri)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Broker URI too long");
        return ESP_ERR_INVALID_ARG;
    }

    web_server_extract_json_string(payload, "\"status_topic\"", topics.status, sizeof(topics.status));
    web_server_extract_json_string(payload, "\"metrics_topic\"", topics.metrics, sizeof(topics.metrics));
    web_server_extract_json_string(payload, "\"config_topic\"", topics.config, sizeof(topics.config));
    web_server_extract_json_string(payload, "\"can_raw_topic\"", topics.can_raw, sizeof(topics.can_raw));
    web_server_extract_json_string(payload, "\"can_decoded_topic\"", topics.can_decoded, sizeof(topics.can_decoded));
    web_server_extract_json_string(payload, "\"can_ready_topic\"", topics.can_ready, sizeof(topics.can_ready));

    esp_err_t err = config_manager_set_mqtt_client_config(&updated);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to update MQTT client");
        return err;
    }

    err = config_manager_set_mqtt_topics(&topics);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to update MQTT topics");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"updated\"}");
}

static esp_err_t web_server_api_mqtt_status_handler(httpd_req_t *req)
{
    mqtt_gateway_status_t status = {0};
    mqtt_gateway_get_status(&status);

    char buffer[WEB_SERVER_MQTT_JSON_SIZE];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"client_started\":%s,\"connected\":%s,\"wifi_connected\":%s,\"reconnects\":%u,\"disconnects\":%u,"
                           "\"errors\":%u,\"last_event_id\":%u,\"last_event\":\"%s\",\"last_event_timestamp_ms\":%llu,"
                           "\"broker_uri\":\"%s\",\"status_topic\":\"%s\",\"metrics_topic\":\"%s\",\"config_topic\":\"%s\","
                           "\"can_raw_topic\":\"%s\",\"can_decoded_topic\":\"%s\",\"can_ready_topic\":\"%s\",\"last_error\":\"%s\"}",
                           status.client_started ? "true" : "false",
                           status.connected ? "true" : "false",
                           status.wifi_connected ? "true" : "false",
                           (unsigned)status.reconnect_count,
                           (unsigned)status.disconnect_count,
                           (unsigned)status.error_count,
                           (unsigned)status.last_event,
                           web_server_mqtt_event_to_string(status.last_event),
                           (unsigned long long)status.last_event_timestamp_ms,
                           status.broker_uri,
                           status.status_topic,
                           status.metrics_topic,
                           status.config_topic,
                           status.can_raw_topic,
                           status.can_decoded_topic,
                           status.can_ready_topic,
                           status.last_error);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "MQTT status too large");
        return ESP_ERR_INVALID_SIZE;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buffer, written);
}

static esp_err_t web_server_api_history_handler(httpd_req_t *req)
{
    size_t limit = 0;
    int query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char query[64];
        if (query_len + 1 > (int)sizeof(query)) {
            query_len = sizeof(query) - 1;
        }
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char value[16];
            if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
                char *endptr = NULL;
                unsigned long parsed = strtoul(value, &endptr, 10);
                if (endptr != value) {
                    limit = (size_t)parsed;
                }
            }
        }
    }

    char buffer[WEB_SERVER_HISTORY_JSON_SIZE];
    size_t length = 0;
    esp_err_t err = monitoring_get_history_json(limit, buffer, sizeof(buffer), &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build history JSON: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "History unavailable");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buffer, length);
}

static esp_err_t web_server_api_registers_get_handler(httpd_req_t *req)
{
    char buffer[CONFIG_MANAGER_MAX_REGISTERS_JSON];
    size_t length = 0;
    esp_err_t err = config_manager_get_registers_json(buffer, sizeof(buffer), &length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build register catalog: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Registers unavailable");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buffer, length);
}

static esp_err_t web_server_api_registers_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_ERR_INVALID_SIZE;
    }

    if (req->content_len + 1 > CONFIG_MANAGER_MAX_CONFIG_SIZE) {
        httpd_resp_send_err(req, HTTPD_413_PAYLOAD_TOO_LARGE, "Register payload too large");
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (ret < 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Error receiving register payload: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        received += ret;
    }

    buffer[received] = '\0';

    esp_err_t err = config_manager_apply_register_update_json(buffer, received);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid register update");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"updated\"}");
}

static esp_err_t web_server_api_ota_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty OTA payload");
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[WEB_SERVER_FILE_BUFSZ];
    size_t received = 0;
    while (received < req->content_len) {
        size_t to_read = req->content_len - received;
        if (to_read > sizeof(buffer)) {
            to_read = sizeof(buffer);
        }

        int ret = httpd_req_recv(req, buffer, to_read);
        if (ret < 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to receive OTA chunk: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        received += ret;
    }

    if (s_event_publisher != NULL) {
        static const char k_ota_message[] = "OTA image uploaded";
        event_bus_event_t event = {
            .id = APP_EVENT_ID_OTA_UPLOAD_READY,
            .payload = k_ota_message,
            .payload_size = sizeof(k_ota_message),
        };
        s_event_publisher(&event, pdMS_TO_TICKS(50));
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"accepted\"}");
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

        char buffer[256];
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
    (void)context;
    if (s_event_subscription == NULL) {
        vTaskDelete(NULL);
        return;
    }

    event_bus_event_t event = {0};
    while (event_bus_receive(s_event_subscription, &event, portMAX_DELAY)) {
        if (event.payload == NULL || event.payload_size == 0) {
            continue;
        }

        const char *payload = (const char *)event.payload;
        size_t length = event.payload_size;

        switch (event.id) {
        case APP_EVENT_ID_TELEMETRY_SAMPLE:
            ws_client_list_broadcast(&s_telemetry_clients, payload, length);
            break;
        case APP_EVENT_ID_UI_NOTIFICATION:
        case APP_EVENT_ID_CONFIG_UPDATED:
        case APP_EVENT_ID_OTA_UPLOAD_READY:
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
        default:
            break;
        }
    }

    vTaskDelete(NULL);
}

void web_server_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
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

    const httpd_uri_t api_history = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = web_server_api_history_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &api_history);

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

    const httpd_uri_t static_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = web_server_static_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &static_files);

    s_event_subscription = event_bus_subscribe_default(NULL, NULL);
    if (s_event_subscription == NULL) {
        ESP_LOGW(TAG, "Failed to subscribe to event bus; WebSocket forwarding disabled");
        return;
    }

    if (xTaskCreate(web_server_event_task, "ws_event", 4096, NULL, 5, &s_event_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start event dispatcher task");
    }
}
