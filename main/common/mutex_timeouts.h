/**
 * @file mutex_timeouts.h
 * @brief Standardized mutex timeout constants
 *
 * This header defines standard timeout values for mutex acquisition
 * across the codebase to ensure consistency and predictable behavior.
 */

#ifndef MUTEX_TIMEOUTS_H
#define MUTEX_TIMEOUTS_H

#ifdef __cplusplus
extern "C" {
#endif

// Mutex timeout constants (in milliseconds)
#define MUTEX_TIMEOUT_CRITICAL_MS   5000  // Init/deinit operations
#define MUTEX_TIMEOUT_NORMAL_MS     1000  // Normal operations
#define MUTEX_TIMEOUT_FAST_MS       100   // Fast path operations
#define MUTEX_TIMEOUT_QUICK_MS      50    // Quick operations (minimal wait)

// Helper macro for common pattern: take mutex or return error
#define TAKE_MUTEX_OR_RETURN(mutex, timeout_ms, retval) \
    do { \
        if (xSemaphoreTake((mutex), pdMS_TO_TICKS(timeout_ms)) != pdTRUE) { \
            ESP_LOGW(TAG, "%s: mutex timeout after %d ms", __func__, (timeout_ms)); \
            return (retval); \
        } \
    } while(0)

// Helper macro: take mutex or return void (for void functions)
#define TAKE_MUTEX_OR_RETURN_VOID(mutex, timeout_ms) \
    do { \
        if (xSemaphoreTake((mutex), pdMS_TO_TICKS(timeout_ms)) != pdTRUE) { \
            ESP_LOGW(TAG, "%s: mutex timeout after %d ms", __func__, (timeout_ms)); \
            return; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif  // MUTEX_TIMEOUTS_H
