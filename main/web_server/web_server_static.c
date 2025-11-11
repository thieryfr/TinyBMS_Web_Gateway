/**
 * @file web_server_static.c
 * @brief Static file serving from SPIFFS filesystem
 *
 * This file contains functionality for:
 * - SPIFFS filesystem mounting
 * - Static file serving (HTML, CSS, JS, images, etc.)
 * - Content-type detection
 * - Path traversal protection
 * - Caching headers
 */

#include "web_server.h"
#include "web_server_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"

#define WEB_SERVER_FS_BASE_PATH "/spiffs"
#define WEB_SERVER_WEB_ROOT     WEB_SERVER_FS_BASE_PATH
#define WEB_SERVER_INDEX_PATH   WEB_SERVER_WEB_ROOT "/index.html"
#define WEB_SERVER_MAX_PATH     256
#define WEB_SERVER_FILE_BUFSZ   1024

static const char *TAG = "web_server_static";

// ============================================================================
// SPIFFS mount
// ============================================================================

esp_err_t web_server_mount_spiffs(void)
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

// ============================================================================
// Content-type detection
// ============================================================================

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

// ============================================================================
// Path traversal protection
// ============================================================================

/**
 * Check if URI is secure (no path traversal attempts)
 * @param uri URI to check
 * @return true if secure, false otherwise
 *
 * Checks for:
 * - ../ and ..\ sequences (including URL encoded variants)
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

// ============================================================================
// File serving
// ============================================================================

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

// ============================================================================
// Request handler
// ============================================================================

esp_err_t web_server_static_get_handler(httpd_req_t *req)
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
