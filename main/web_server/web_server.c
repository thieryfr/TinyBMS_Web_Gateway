#include "web_server.h"

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

    const httpd_uri_t static_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = web_server_static_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &static_files);

    s_event_subscription = event_bus_subscribe(8, NULL, NULL);
    if (s_event_subscription == NULL) {
        ESP_LOGW(TAG, "Failed to subscribe to event bus; WebSocket forwarding disabled");
        return;
    }

    if (xTaskCreate(web_server_event_task, "ws_event", 4096, NULL, 5, &s_event_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start event dispatcher task");
    }
}
