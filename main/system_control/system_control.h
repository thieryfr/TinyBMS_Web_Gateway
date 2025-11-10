#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSTEM_CONTROL_TARGET_BMS,
    SYSTEM_CONTROL_TARGET_GATEWAY,
} system_control_target_t;

/**
 * @brief Request a graceful restart of the TinyBMS controller over UART.
 *
 * The function throttles restart commands to avoid spamming the bus and
 * guarantees idempotence by rejecting duplicate requests sent within a short
 * window.
 *
 * @param timeout_ms Optional response timeout (0 selects the default).
 * @return ESP_OK when the command was sent, or an esp_err_t code otherwise.
 */
esp_err_t system_control_request_bms_restart(uint32_t timeout_ms);

/**
 * @brief Schedule a restart of the ESP32 gateway after a configurable delay.
 *
 * The restart is executed asynchronously allowing the HTTP handler to respond
 * before the device reboots.
 *
 * @param delay_ms Delay in milliseconds before triggering esp_restart().
 * @return ESP_OK when the restart was scheduled successfully.
 */
esp_err_t system_control_schedule_gateway_restart(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

