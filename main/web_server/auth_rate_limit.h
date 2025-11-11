/**
 * @file auth_rate_limit.h
 * @brief Authentication rate limiting for brute-force protection
 *
 * This module provides protection against brute-force authentication attacks
 * by limiting the rate of authentication attempts per IP address.
 *
 * Features:
 * - Per-IP rate limiting with exponential backoff
 * - Automatic lockout after repeated failures
 * - Configurable thresholds and timeouts
 * - Memory-efficient circular buffer
 *
 * Security:
 * - Default: 5 failures → 60s lockout
 * - Exponential backoff: 1s, 5s, 15s, 30s, 60s, 300s
 * - Automatic cleanup of old entries
 */

#ifndef AUTH_RATE_LIMIT_H
#define AUTH_RATE_LIMIT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum authentication attempts before lockout
 *
 * After this many consecutive failures, the IP is locked out.
 */
#ifndef CONFIG_TINYBMS_AUTH_MAX_ATTEMPTS
#define CONFIG_TINYBMS_AUTH_MAX_ATTEMPTS 5
#endif

/**
 * @brief Initial lockout duration (milliseconds)
 *
 * After first failure threshold, IP is locked for this duration.
 */
#ifndef CONFIG_TINYBMS_AUTH_LOCKOUT_MS
#define CONFIG_TINYBMS_AUTH_LOCKOUT_MS 60000  // 60 seconds
#endif

/**
 * @brief Enable exponential backoff for repeated failures
 *
 * Each subsequent failure increases lockout duration exponentially.
 */
#ifndef CONFIG_TINYBMS_AUTH_EXPONENTIAL_BACKOFF
#define CONFIG_TINYBMS_AUTH_EXPONENTIAL_BACKOFF 1
#endif

/**
 * @brief Maximum tracked IP addresses
 *
 * Circular buffer size for tracking failed authentication attempts.
 * Oldest entries are evicted when buffer is full.
 */
#define AUTH_RATE_LIMIT_MAX_IPS 20

/**
 * @brief Initialize rate limiting module
 *
 * Must be called before any other auth_rate_limit_* functions.
 *
 * @return ESP_OK on success
 */
esp_err_t auth_rate_limit_init(void);

/**
 * @brief Check if IP address is allowed to attempt authentication
 *
 * Returns true if IP can attempt authentication, false if locked out.
 *
 * @param ip_addr IP address (IPv4 as uint32_t, network byte order)
 * @param[out] lockout_remaining_ms Time remaining in lockout (optional)
 * @return true if allowed, false if locked out
 */
bool auth_rate_limit_check(uint32_t ip_addr, uint32_t *lockout_remaining_ms);

/**
 * @brief Record successful authentication (clears failures)
 *
 * Resets failure count for the IP address.
 *
 * @param ip_addr IP address (IPv4 as uint32_t, network byte order)
 */
void auth_rate_limit_success(uint32_t ip_addr);

/**
 * @brief Record failed authentication attempt
 *
 * Increments failure count and applies lockout if threshold exceeded.
 *
 * @param ip_addr IP address (IPv4 as uint32_t, network byte order)
 */
void auth_rate_limit_failure(uint32_t ip_addr);

/**
 * @brief Get current failure count for IP
 *
 * @param ip_addr IP address (IPv4 as uint32_t, network byte order)
 * @return Number of consecutive failures
 */
uint8_t auth_rate_limit_get_failures(uint32_t ip_addr);

/**
 * @brief Manually unlock an IP address
 *
 * Admin function to clear lockout for an IP.
 *
 * @param ip_addr IP address (IPv4 as uint32_t, network byte order)
 */
void auth_rate_limit_unlock(uint32_t ip_addr);

/**
 * @brief Clear all rate limit data
 *
 * Resets all failure counts and lockouts.
 */
void auth_rate_limit_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif  // AUTH_RATE_LIMIT_H
