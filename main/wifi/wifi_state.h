#pragma once

/**
 * @file wifi_state.h
 * @brief Shared Wi-Fi runtime state description used across Wi-Fi modules.
 */

#include <stdbool.h>

#include "event_bus.h"

#ifdef ESP_PLATFORM
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#endif

/**
 * @brief Aggregated runtime state for the Wi-Fi subsystem.
 *
 * The structure is intentionally organised to make responsibilities explicit:
 * - Station/Access Point coordination (retry counter, fallback flag);
 * - Timer management for STA retry attempts;
 * - Event publication hooks for the application event bus;
 * - Synchronisation primitives protecting mutable fields when running on the
 *   target platform.
 */
typedef struct wifi_shared_state {
    bool initialised;                 /**< Wi-Fi driver initialisation flag. */
    bool ap_fallback_active;          /**< True when fallback AP mode is active. */
    int retry_count;                  /**< STA reconnection attempt counter. */
    event_bus_publish_fn_t publisher; /**< Event bus publish hook. */
#ifdef ESP_PLATFORM
    esp_netif_t *sta_netif; /**< Station network interface instance. */
    esp_netif_t *ap_netif;  /**< Access point network interface instance. */
    esp_event_handler_instance_t wifi_event_handle; /**< Wi-Fi event handler */
    esp_event_handler_instance_t ip_got_handle;     /**< IP acquisition handler */
    esp_event_handler_instance_t ip_lost_handle;    /**< IP loss handler */
    TimerHandle_t sta_retry_timer;                  /**< STA retry timer handle */
    SemaphoreHandle_t mutex;                        /**< State protection mutex */
#endif
} wifi_shared_state_t;

/**
 * @brief Reset mutable runtime fields while keeping external bindings intact.
 */
void wifi_state_reset(wifi_shared_state_t *state);

/**
 * @brief Clear the registered event publisher.
 */
void wifi_state_clear_publisher(wifi_shared_state_t *state);
