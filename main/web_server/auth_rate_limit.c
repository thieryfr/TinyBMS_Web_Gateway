/**
 * @file auth_rate_limit.c
 * @brief Authentication rate limiting implementation
 */

#include "auth_rate_limit.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
#define ESP_LOGI(tag, fmt, ...) (void)tag
#define ESP_LOGW(tag, fmt, ...) (void)tag
#define ESP_LOGE(tag, fmt, ...) (void)tag
static inline int64_t esp_timer_get_time(void) { return 0; }
#endif

static const char *TAG = "auth_rate_limit";

/**
 * @brief Rate limit entry for a single IP address
 */
typedef struct {
    uint32_t ip_addr;           // IPv4 address (network byte order)
    uint8_t failure_count;      // Consecutive failure count
    int64_t last_failure_time;  // Timestamp of last failure (microseconds)
    uint32_t lockout_duration;  // Current lockout duration (milliseconds)
    bool is_locked;             // Currently locked out
} rate_limit_entry_t;

/**
 * @brief Circular buffer for rate limit entries
 */
typedef struct {
    rate_limit_entry_t entries[AUTH_RATE_LIMIT_MAX_IPS];
    size_t head;                // Next write position
    size_t count;               // Number of active entries
    SemaphoreHandle_t lock;     // Mutex for thread safety
} rate_limit_ctx_t;

static rate_limit_ctx_t s_ctx = {
    .entries = {0},
    .head = 0,
    .count = 0,
    .lock = NULL,
};

// Exponential backoff durations (milliseconds)
static const uint32_t s_lockout_durations[] = {
    1000,    // 1st failure:  1 second
    5000,    // 2nd failure:  5 seconds
    15000,   // 3rd failure: 15 seconds
    30000,   // 4th failure: 30 seconds
    60000,   // 5th failure: 60 seconds (1 minute)
    300000,  // 6th+ failure: 300 seconds (5 minutes)
};

#define LOCKOUT_DURATIONS_COUNT (sizeof(s_lockout_durations) / sizeof(s_lockout_durations[0]))

esp_err_t auth_rate_limit_init(void)
{
    if (s_ctx.lock != NULL) {
        return ESP_OK;  // Already initialized
    }

#ifdef ESP_PLATFORM
    s_ctx.lock = xSemaphoreCreateMutex();
    if (s_ctx.lock == NULL) {
        ESP_LOGE(TAG, "Failed to create rate limit mutex");
        return ESP_ERR_NO_MEM;
    }
#endif

    memset(s_ctx.entries, 0, sizeof(s_ctx.entries));
    s_ctx.head = 0;
    s_ctx.count = 0;

    ESP_LOGI(TAG, "Auth rate limiting initialized (max_attempts=%d, lockout=%dms)",
             CONFIG_TINYBMS_AUTH_MAX_ATTEMPTS,
             CONFIG_TINYBMS_AUTH_LOCKOUT_MS);

    return ESP_OK;
}

/**
 * @brief Find entry for IP address (internal, assumes lock held)
 */
static rate_limit_entry_t* find_entry_locked(uint32_t ip_addr)
{
    for (size_t i = 0; i < s_ctx.count; i++) {
        if (s_ctx.entries[i].ip_addr == ip_addr) {
            return &s_ctx.entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Create or get entry for IP (internal, assumes lock held)
 */
static rate_limit_entry_t* get_or_create_entry_locked(uint32_t ip_addr)
{
    // Check if entry exists
    rate_limit_entry_t *entry = find_entry_locked(ip_addr);
    if (entry != NULL) {
        return entry;
    }

    // Create new entry
    if (s_ctx.count < AUTH_RATE_LIMIT_MAX_IPS) {
        // Buffer not full - use next slot
        entry = &s_ctx.entries[s_ctx.count];
        s_ctx.count++;
    } else {
        // Buffer full - evict oldest (circular buffer)
        entry = &s_ctx.entries[s_ctx.head];
        s_ctx.head = (s_ctx.head + 1) % AUTH_RATE_LIMIT_MAX_IPS;
    }

    // Initialize new entry
    memset(entry, 0, sizeof(*entry));
    entry->ip_addr = ip_addr;
    return entry;
}

/**
 * @brief Calculate lockout duration based on failure count
 */
static uint32_t calculate_lockout_duration(uint8_t failure_count)
{
#if CONFIG_TINYBMS_AUTH_EXPONENTIAL_BACKOFF
    // Exponential backoff
    size_t index = (failure_count > 0) ? (failure_count - 1) : 0;
    if (index >= LOCKOUT_DURATIONS_COUNT) {
        index = LOCKOUT_DURATIONS_COUNT - 1;
    }
    return s_lockout_durations[index];
#else
    // Fixed duration
    (void)failure_count;
    return CONFIG_TINYBMS_AUTH_LOCKOUT_MS;
#endif
}

bool auth_rate_limit_check(uint32_t ip_addr, uint32_t *lockout_remaining_ms)
{
    if (lockout_remaining_ms != NULL) {
        *lockout_remaining_ms = 0;
    }

#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return true;  // Not initialized - allow
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire rate limit lock");
        return true;  // Fail open (allow access if lock unavailable)
    }
#endif

    rate_limit_entry_t *entry = find_entry_locked(ip_addr);
    if (entry == NULL || !entry->is_locked) {
#ifdef ESP_PLATFORM
        xSemaphoreGive(s_ctx.lock);
#endif
        return true;  // No entry or not locked - allow
    }

    // Check if lockout has expired
    int64_t now = esp_timer_get_time();
    int64_t lockout_end = entry->last_failure_time + (entry->lockout_duration * 1000LL);

    if (now >= lockout_end) {
        // Lockout expired - allow and reset
        entry->is_locked = false;
        entry->failure_count = 0;
#ifdef ESP_PLATFORM
        xSemaphoreGive(s_ctx.lock);
#endif
        return true;
    }

    // Still locked out
    if (lockout_remaining_ms != NULL) {
        int64_t remaining_us = lockout_end - now;
        *lockout_remaining_ms = (uint32_t)(remaining_us / 1000LL);
    }

    uint8_t ip_bytes[4];
    memcpy(ip_bytes, &ip_addr, 4);
    ESP_LOGW(TAG, "⚠️  IP %d.%d.%d.%d locked out (%d failures, %ums remaining)",
             ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
             entry->failure_count,
             (lockout_remaining_ms != NULL) ? *lockout_remaining_ms : 0);

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif
    return false;
}

void auth_rate_limit_success(uint32_t ip_addr)
{
#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
#endif

    rate_limit_entry_t *entry = find_entry_locked(ip_addr);
    if (entry != NULL) {
        uint8_t ip_bytes[4];
        memcpy(ip_bytes, &ip_addr, 4);

        if (entry->failure_count > 0) {
            ESP_LOGI(TAG, "✓ Successful auth from %d.%d.%d.%d (cleared %d failures)",
                     ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                     entry->failure_count);
        }

        // Clear failures on success
        entry->failure_count = 0;
        entry->is_locked = false;
        entry->lockout_duration = 0;
    }

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif
}

void auth_rate_limit_failure(uint32_t ip_addr)
{
#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
#endif

    rate_limit_entry_t *entry = get_or_create_entry_locked(ip_addr);
    if (entry == NULL) {
#ifdef ESP_PLATFORM
        xSemaphoreGive(s_ctx.lock);
#endif
        return;
    }

    entry->failure_count++;
    entry->last_failure_time = esp_timer_get_time();

    uint8_t ip_bytes[4];
    memcpy(ip_bytes, &ip_addr, 4);

    // Check if lockout threshold reached
    if (entry->failure_count >= CONFIG_TINYBMS_AUTH_MAX_ATTEMPTS) {
        entry->is_locked = true;
        entry->lockout_duration = calculate_lockout_duration(entry->failure_count);

        ESP_LOGW(TAG, "🔒 IP %d.%d.%d.%d LOCKED OUT (%d failures, %ums lockout)",
                 ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                 entry->failure_count,
                 entry->lockout_duration);
    } else {
        ESP_LOGI(TAG, "⚠️  Auth failure from %d.%d.%d.%d (%d/%d attempts)",
                 ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                 entry->failure_count,
                 CONFIG_TINYBMS_AUTH_MAX_ATTEMPTS);
    }

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif
}

uint8_t auth_rate_limit_get_failures(uint32_t ip_addr)
{
    uint8_t count = 0;

#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return 0;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
#endif

    rate_limit_entry_t *entry = find_entry_locked(ip_addr);
    if (entry != NULL) {
        count = entry->failure_count;
    }

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif

    return count;
}

void auth_rate_limit_unlock(uint32_t ip_addr)
{
#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
#endif

    rate_limit_entry_t *entry = find_entry_locked(ip_addr);
    if (entry != NULL) {
        uint8_t ip_bytes[4];
        memcpy(ip_bytes, &ip_addr, 4);

        ESP_LOGI(TAG, "🔓 Manually unlocked IP %d.%d.%d.%d",
                 ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);

        entry->failure_count = 0;
        entry->is_locked = false;
        entry->lockout_duration = 0;
    }

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif
}

void auth_rate_limit_clear_all(void)
{
#ifdef ESP_PLATFORM
    if (s_ctx.lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
#endif

    memset(s_ctx.entries, 0, sizeof(s_ctx.entries));
    s_ctx.head = 0;
    s_ctx.count = 0;

    ESP_LOGI(TAG, "All rate limit data cleared");

#ifdef ESP_PLATFORM
    xSemaphoreGive(s_ctx.lock);
#endif
}
