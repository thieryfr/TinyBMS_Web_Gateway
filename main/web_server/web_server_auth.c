/**
 * @file web_server_auth.c
 * @brief Web server authentication, CSRF protection, and rate limiting
 *
 * This file contains all authentication-related functionality including:
 * - HTTP Basic authentication
 * - CSRF token management
 * - Password hashing and validation
 * - Credentials storage in NVS
 * - Rate limiting integration
 */

#include "web_server.h"
#include "web_server_internal.h"

#include <ctype.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"

#include "sdkconfig.h"
#include "auth_rate_limit.h"

#define WEB_SERVER_AUTH_NAMESPACE              "web_auth"
#define WEB_SERVER_AUTH_USERNAME_KEY           "username"
#define WEB_SERVER_AUTH_SALT_KEY               "salt"
#define WEB_SERVER_AUTH_HASH_KEY               "password_hash"

static const char *TAG = "web_server_auth";

#if CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE

// ============================================================================
// Global state variables (authentication)
// ============================================================================

bool g_basic_auth_enabled = false;
SemaphoreHandle_t g_auth_mutex = NULL;

static char s_basic_auth_username[WEB_SERVER_AUTH_MAX_USERNAME_LENGTH + 1];
static uint8_t s_basic_auth_salt[WEB_SERVER_AUTH_SALT_SIZE];
static uint8_t s_basic_auth_hash[WEB_SERVER_AUTH_HASH_SIZE];
static web_server_csrf_token_t s_csrf_tokens[WEB_SERVER_CSRF_MAX_TOKENS];

// ============================================================================
// Helper functions
// ============================================================================

static void web_server_generate_random_bytes(uint8_t *buffer, size_t size)
{
    if (buffer == NULL) {
        return;
    }

    for (size_t offset = 0; offset < size;) {
        uint32_t value = esp_random();
        size_t chunk = sizeof(value);
        if (chunk > (size - offset)) {
            chunk = size - offset;
        }
        memcpy(buffer + offset, &value, chunk);
        offset += chunk;
    }
}

static void web_server_auth_compute_hash(const uint8_t *salt, const char *password, uint8_t *out_hash)
{
    if (salt == NULL || password == NULL || out_hash == NULL) {
        return;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return;
    }

    (void)mbedtls_sha256_update_ret(&ctx, salt, WEB_SERVER_AUTH_SALT_SIZE);
    (void)mbedtls_sha256_update_ret(&ctx, (const unsigned char *)password, strlen(password));
    (void)mbedtls_sha256_finish_ret(&ctx, out_hash);
    mbedtls_sha256_free(&ctx);
}

// ============================================================================
// Response helpers
// ============================================================================

void web_server_send_unauthorized(httpd_req_t *req)
{
    if (req == NULL) {
        return;
    }

    web_server_set_security_headers(req);
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"TinyBMS-GW\", charset=\"UTF-8\"");
    httpd_resp_send(req, "{\"error\":\"authentication_required\"}", HTTPD_RESP_USE_STRLEN);
}

void web_server_send_forbidden(httpd_req_t *req, const char *message)
{
    if (req == NULL) {
        return;
    }

    web_server_set_security_headers(req);
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const char *error = (message != NULL) ? message : "forbidden";
    char buffer[96];
    int written = snprintf(buffer, sizeof(buffer), "{\"error\":\"%s\"}", error);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        httpd_resp_send(req, "{\"error\":\"forbidden\"}", HTTPD_RESP_USE_STRLEN);
        return;
    }
    httpd_resp_send(req, buffer, written);
}

// ============================================================================
// Credentials management
// ============================================================================

static esp_err_t web_server_auth_store_default_locked(nvs_handle_t handle)
{
    const char *default_username = CONFIG_TINYBMS_WEB_AUTH_USERNAME;
    const char *default_password = CONFIG_TINYBMS_WEB_AUTH_PASSWORD;

    size_t username_len = strnlen(default_username, sizeof(s_basic_auth_username));
    size_t password_len = strnlen(default_password, WEB_SERVER_AUTH_MAX_PASSWORD_LENGTH + 1U);

    if (username_len == 0 || username_len >= sizeof(s_basic_auth_username) ||
        password_len == 0 || password_len > WEB_SERVER_AUTH_MAX_PASSWORD_LENGTH) {
        ESP_LOGE(TAG, "Invalid default HTTP credentials length");
        return ESP_ERR_INVALID_ARG;
    }

    memset(s_basic_auth_username, 0, sizeof(s_basic_auth_username));
    memcpy(s_basic_auth_username, default_username, username_len);

    web_server_generate_random_bytes(s_basic_auth_salt, sizeof(s_basic_auth_salt));
    web_server_auth_compute_hash(s_basic_auth_salt, default_password, s_basic_auth_hash);

    esp_err_t err = nvs_set_str(handle, WEB_SERVER_AUTH_USERNAME_KEY, s_basic_auth_username);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store default username: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, WEB_SERVER_AUTH_SALT_KEY, s_basic_auth_salt, sizeof(s_basic_auth_salt));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store auth salt: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, WEB_SERVER_AUTH_HASH_KEY, s_basic_auth_hash, sizeof(s_basic_auth_hash));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store auth hash: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit auth credentials: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Provisioned default HTTP credentials for user '%s'", s_basic_auth_username);
    return ESP_OK;
}

static esp_err_t web_server_auth_load_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WEB_SERVER_AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", WEB_SERVER_AUTH_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    bool provision_defaults = false;

    size_t username_len = sizeof(s_basic_auth_username);
    err = nvs_get_str(handle, WEB_SERVER_AUTH_USERNAME_KEY, s_basic_auth_username, &username_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        provision_defaults = true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load auth username: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    size_t salt_len = sizeof(s_basic_auth_salt);
    err = nvs_get_blob(handle, WEB_SERVER_AUTH_SALT_KEY, s_basic_auth_salt, &salt_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        provision_defaults = true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load auth salt: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    } else if (salt_len != sizeof(s_basic_auth_salt)) {
        ESP_LOGW(TAG, "Invalid auth salt length (%u)", (unsigned)salt_len);
        provision_defaults = true;
    }

    size_t hash_len = sizeof(s_basic_auth_hash);
    err = nvs_get_blob(handle, WEB_SERVER_AUTH_HASH_KEY, s_basic_auth_hash, &hash_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        provision_defaults = true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load auth hash: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    } else if (hash_len != sizeof(s_basic_auth_hash)) {
        ESP_LOGW(TAG, "Invalid auth hash length (%u)", (unsigned)hash_len);
        provision_defaults = true;
    }

    if (provision_defaults) {
        if (g_auth_mutex == NULL || xSemaphoreTake(g_auth_mutex, pdMS_TO_TICKS(WEB_SERVER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to acquire auth mutex (timeout)");
            nvs_close(handle);
            return ESP_ERR_TIMEOUT;
        }
        err = web_server_auth_store_default_locked(handle);
        xSemaphoreGive(g_auth_mutex);
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }
    }

    s_basic_auth_username[sizeof(s_basic_auth_username) - 1U] = '\0';
    nvs_close(handle);
    return ESP_OK;
}

// ============================================================================
// Authentication
// ============================================================================

static bool web_server_basic_authenticate(const char *username, const char *password)
{
    if (!g_basic_auth_enabled || username == NULL || password == NULL) {
        return false;
    }

    bool authorized = false;
    if (g_auth_mutex == NULL || xSemaphoreTake(g_auth_mutex, pdMS_TO_TICKS(WEB_SERVER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire auth mutex for verification (timeout)");
        return false;
    }

    if (strncmp(username, s_basic_auth_username, sizeof(s_basic_auth_username)) == 0) {
        uint8_t computed[WEB_SERVER_AUTH_HASH_SIZE];
        web_server_auth_compute_hash(s_basic_auth_salt, password, computed);
        authorized = (memcmp(computed, s_basic_auth_hash, sizeof(computed)) == 0);
        memset(computed, 0, sizeof(computed));
    }

    xSemaphoreGive(g_auth_mutex);
    return authorized;
}

static bool web_server_require_basic_auth(httpd_req_t *req, char *out_username, size_t out_size)
{
    // Extract client IP address for rate limiting
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in6 addr;
    socklen_t addr_size = sizeof(addr);
    uint32_t client_ip = 0;

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) == 0) {
        if (addr.sin6_family == AF_INET) {
            // IPv4
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            client_ip = s->sin_addr.s_addr;
        } else if (addr.sin6_family == AF_INET6) {
            // IPv6 - use hash of address as pseudo-IPv4 for rate limiting
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
            for (int i = 0; i < 16; i += 4) {
                client_ip ^= *((uint32_t *)&s->sin6_addr.s6_addr[i]);
            }
        }
    }

    // Check rate limiting BEFORE processing credentials
    uint32_t lockout_remaining_ms = 0;
    if (!auth_rate_limit_check(client_ip, &lockout_remaining_ms)) {
        // IP is locked out - reject immediately
        char retry_after[32];
        snprintf(retry_after, sizeof(retry_after), "%u", (lockout_remaining_ms + 999) / 1000);
        httpd_resp_set_hdr(req, "Retry-After", retry_after);
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"too_many_attempts\",\"retry_after_seconds\":" , HTTPD_RESP_USE_STRLEN);
        char lockout_json[64];
        snprintf(lockout_json, sizeof(lockout_json), "%u}", (lockout_remaining_ms + 999) / 1000);
        httpd_resp_sendstr_chunk(req, lockout_json);
        httpd_resp_sendstr_chunk(req, NULL);
        return false;
    }

    size_t header_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (header_len == 0 || header_len >= WEB_SERVER_AUTH_HEADER_MAX) {
        auth_rate_limit_failure(client_ip);  // Missing header = failed attempt
        web_server_send_unauthorized(req);
        return false;
    }

    char header[WEB_SERVER_AUTH_HEADER_MAX];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    const char *value = header;
    while (isspace((unsigned char)*value)) {
        ++value;
    }

    if (strncasecmp(value, "Basic ", 6) != 0) {
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    value += 6;
    while (isspace((unsigned char)*value)) {
        ++value;
    }

    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &decoded_len, (const unsigned char *)value, strlen(value));
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0) {
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    if (decoded_len == 0 || decoded_len >= WEB_SERVER_AUTH_DECODED_MAX) {
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    char decoded[WEB_SERVER_AUTH_DECODED_MAX];
    ret = mbedtls_base64_decode((unsigned char *)decoded,
                                sizeof(decoded) - 1U,
                                &decoded_len,
                                (const unsigned char *)value,
                                strlen(value));
    if (ret != 0) {
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }
    decoded[decoded_len] = '\0';

    char *separator = strchr(decoded, ':');
    if (separator == NULL) {
        memset(decoded, 0, sizeof(decoded));
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    *separator = '\0';
    const char *username = decoded;
    const char *password = separator + 1;

    if (username[0] == '\0' || password[0] == '\0') {
        memset(decoded, 0, sizeof(decoded));
        auth_rate_limit_failure(client_ip);
        web_server_send_unauthorized(req);
        return false;
    }

    char password_copy[WEB_SERVER_AUTH_MAX_PASSWORD_LENGTH + 1];
    snprintf(password_copy, sizeof(password_copy), "%s", password);

    bool authorized = web_server_basic_authenticate(username, password_copy);
    memset(password_copy, 0, sizeof(password_copy));
    memset(decoded, 0, sizeof(decoded));

    if (!authorized) {
        auth_rate_limit_failure(client_ip);  // Record failed authentication
        web_server_send_unauthorized(req);
        return false;
    }

    // Authentication successful - clear rate limit
    auth_rate_limit_success(client_ip);

    if (out_username != NULL && out_size > 0U) {
        snprintf(out_username, out_size, "%s", username);
    }

    return true;
}

// ============================================================================
// CSRF token management
// ============================================================================

static web_server_csrf_token_t *web_server_find_or_allocate_csrf_entry(const char *username, int64_t now_us)
{
    size_t candidate = WEB_SERVER_CSRF_MAX_TOKENS;
    int64_t oldest = INT64_MAX;

    for (size_t i = 0; i < WEB_SERVER_CSRF_MAX_TOKENS; ++i) {
        web_server_csrf_token_t *entry = &s_csrf_tokens[i];
        if (!entry->in_use || entry->expires_at_us <= now_us) {
            candidate = i;
            break;
        }
        if (strncmp(entry->username, username, sizeof(entry->username)) == 0) {
            candidate = i;
            break;
        }
        if (entry->expires_at_us < oldest) {
            oldest = entry->expires_at_us;
            candidate = i;
        }
    }

    if (candidate >= WEB_SERVER_CSRF_MAX_TOKENS) {
        candidate = 0;
    }

    return &s_csrf_tokens[candidate];
}

bool web_server_issue_csrf_token(const char *username, char *out_token, size_t out_size, uint32_t *out_ttl_ms)
{
    if (username == NULL || g_auth_mutex == NULL) {
        return false;
    }

    uint8_t random_bytes[WEB_SERVER_CSRF_TOKEN_SIZE];
    web_server_generate_random_bytes(random_bytes, sizeof(random_bytes));

    char token[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH + 1];
    for (size_t i = 0; i < WEB_SERVER_CSRF_TOKEN_SIZE; ++i) {
        static const char hex[] = "0123456789abcdef";
        token[(size_t)2U * i] = hex[random_bytes[i] >> 4];
        token[(size_t)2U * i + 1U] = hex[random_bytes[i] & 0x0F];
    }
    token[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH] = '\0';

    int64_t now_us = esp_timer_get_time();
    int64_t expires_at = now_us + WEB_SERVER_CSRF_TOKEN_TTL_MS * 1000ULL;

    if (xSemaphoreTake(g_auth_mutex, pdMS_TO_TICKS(WEB_SERVER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire auth mutex for CSRF creation (timeout)");
        return false;
    }

    web_server_csrf_token_t *entry = web_server_find_or_allocate_csrf_entry(username, now_us);
    entry->in_use = true;
    snprintf(entry->username, sizeof(entry->username), "%s", username);
    snprintf(entry->token, sizeof(entry->token), "%s", token);
    entry->expires_at_us = expires_at;

    xSemaphoreGive(g_auth_mutex);

    if (out_token != NULL && out_size > 0U) {
        snprintf(out_token, out_size, "%s", token);
    }
    if (out_ttl_ms != NULL) {
        *out_ttl_ms = WEB_SERVER_CSRF_TOKEN_TTL_MS;
    }

    memset(random_bytes, 0, sizeof(random_bytes));
    return true;
}

static bool web_server_validate_csrf_token(const char *username, const char *token)
{
    if (username == NULL || token == NULL || g_auth_mutex == NULL) {
        return false;
    }

    bool valid = false;
    int64_t now_us = esp_timer_get_time();

    if (xSemaphoreTake(g_auth_mutex, pdMS_TO_TICKS(WEB_SERVER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire auth mutex for CSRF validation (timeout)");
        return false;
    }

    for (size_t i = 0; i < WEB_SERVER_CSRF_MAX_TOKENS; ++i) {
        web_server_csrf_token_t *entry = &s_csrf_tokens[i];
        if (!entry->in_use) {
            continue;
        }
        if (entry->expires_at_us <= now_us) {
            entry->in_use = false;
            continue;
        }
        if (strncmp(entry->username, username, sizeof(entry->username)) == 0 &&
            strncmp(entry->token, token, sizeof(entry->token)) == 0) {
            entry->expires_at_us = now_us + WEB_SERVER_CSRF_TOKEN_TTL_MS * 1000ULL;
            valid = true;
            break;
        }
    }

    xSemaphoreGive(g_auth_mutex);
    return valid;
}

static bool web_server_validate_csrf_header(httpd_req_t *req, const char *username)
{
    size_t token_len = httpd_req_get_hdr_value_len(req, "X-CSRF-Token");
    if (token_len == 0 || token_len > WEB_SERVER_CSRF_TOKEN_STRING_LENGTH) {
        web_server_send_forbidden(req, "csrf_token_required");
        return false;
    }

    char token[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH + 1];
    if (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", token, sizeof(token)) != ESP_OK) {
        web_server_send_forbidden(req, "csrf_token_missing");
        return false;
    }

    if (!web_server_validate_csrf_token(username, token)) {
        web_server_send_forbidden(req, "csrf_token_invalid");
        return false;
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

void web_server_auth_init(void)
{
    if (g_auth_mutex == NULL) {
        g_auth_mutex = xSemaphoreCreateMutex();
        if (g_auth_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create auth mutex");
            return;
        }
    }

    esp_err_t err = web_server_auth_load_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP authentication disabled due to credential load error");
        g_basic_auth_enabled = false;
        return;
    }

    memset(s_csrf_tokens, 0, sizeof(s_csrf_tokens));
    g_basic_auth_enabled = true;

    // Initialize rate limiting for brute-force protection
    esp_err_t rate_limit_err = auth_rate_limit_init();
    if (rate_limit_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize auth rate limiting: %s", esp_err_to_name(rate_limit_err));
    } else {
        ESP_LOGI(TAG, "✓ Auth rate limiting enabled (brute-force protection)");
    }

    ESP_LOGI(TAG, "HTTP Basic authentication enabled");
}

bool web_server_require_authorization(httpd_req_t *req, bool require_csrf, char *out_username, size_t out_size)
{
    if (!g_basic_auth_enabled) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "Authentication unavailable");
        return false;
    }

    char username[WEB_SERVER_AUTH_MAX_USERNAME_LENGTH + 1];
    char *username_ptr = (out_username != NULL) ? out_username : username;
    size_t username_capacity = (out_username != NULL) ? out_size : sizeof(username);

    if (!web_server_require_basic_auth(req, username_ptr, username_capacity)) {
        return false;
    }

    if (require_csrf) {
        return web_server_validate_csrf_header(req, username_ptr);
    }

    return true;
}

esp_err_t web_server_api_security_csrf_get_handler(httpd_req_t *req)
{
    char username[WEB_SERVER_AUTH_MAX_USERNAME_LENGTH + 1];
    if (!web_server_require_authorization(req, false, username, sizeof(username))) {
        return ESP_FAIL;
    }

    char token[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH + 1];
    uint32_t ttl_ms = 0U;
    if (!web_server_issue_csrf_token(username, token, sizeof(token), &ttl_ms)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to issue CSRF token");
        return ESP_FAIL;
    }

    char response[WEB_SERVER_CSRF_TOKEN_STRING_LENGTH + 64];
    int written = snprintf(response,
                           sizeof(response),
                           "{\"token\":\"%s\",\"expires_in\":%u}",
                           token,
                           (unsigned)ttl_ms);
    if (written < 0 || written >= (int)sizeof(response)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to encode token");
        return ESP_ERR_INVALID_SIZE;
    }

    web_server_set_security_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, written);
}

#endif  // CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE
