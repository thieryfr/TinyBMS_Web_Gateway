/**
 * @file can_config_defaults.h
 * @brief Centralized CAN configuration defaults for TinyBMS-GW
 *
 * This file contains all default configuration values for CAN bus functionality.
 * All CAN-related modules should include this file to ensure consistency.
 *
 * DO NOT duplicate these values in individual modules!
 */

#ifndef CAN_CONFIG_DEFAULTS_H
#define CAN_CONFIG_DEFAULTS_H

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CAN GPIO Pin Configuration
// =============================================================================

#ifndef CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO
#define CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO 7
#endif

#ifndef CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO
#define CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO 6
#endif

// =============================================================================
// CAN Timing Configuration
// =============================================================================

#ifndef CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS
#define CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS 1000
#endif

#ifndef CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS
#define CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS 600000  // 10 minutes as per Victron spec
#endif

#ifndef CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS
#define CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS 500
#endif

#ifndef CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS
#define CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS 0
#endif

// =============================================================================
// CAN Protocol Configuration
// =============================================================================

#ifndef CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII
#define CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII "VIC"
#endif

#ifndef CONFIG_TINYBMS_CAN_MANUFACTURER
#define CONFIG_TINYBMS_CAN_MANUFACTURER "TinyBMS"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_NAME
#define CONFIG_TINYBMS_CAN_BATTERY_NAME "Lithium Battery"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_FAMILY
#define CONFIG_TINYBMS_CAN_BATTERY_FAMILY CONFIG_TINYBMS_CAN_BATTERY_NAME
#endif

#ifdef __cplusplus
}
#endif

#endif // CAN_CONFIG_DEFAULTS_H
