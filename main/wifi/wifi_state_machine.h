#pragma once

#include "config_manager.h"
#include "wifi_events.h"
#include "wifi_state.h"

/**
 * @brief Enumeration of internal Wi-Fi state transitions used by the shared
 *        state machine. These transitions are triggered from ESP-IDF event
 *        callbacks and exposed for unit tests to simulate behaviour.
 */
typedef enum {
    WIFI_STATE_TRANSITION_STA_START,
    WIFI_STATE_TRANSITION_STA_CONNECTED,
    WIFI_STATE_TRANSITION_STA_DISCONNECTED,
    WIFI_STATE_TRANSITION_STA_GOT_IP,
    WIFI_STATE_TRANSITION_STA_LOST_IP,
    WIFI_STATE_TRANSITION_AP_STARTED,
    WIFI_STATE_TRANSITION_AP_STOPPED,
    WIFI_STATE_TRANSITION_AP_FAILED,
    WIFI_STATE_TRANSITION_AP_CLIENT_CONNECTED,
    WIFI_STATE_TRANSITION_AP_CLIENT_DISCONNECTED,
} wifi_state_transition_t;

/**
 * @brief Minimal information used when handling STA disconnection events.
 */
typedef struct {
    int reason;
} wifi_state_disconnected_info_t;

void wifi_state_machine_init(wifi_shared_state_t *state);
void wifi_state_machine_deinit(wifi_shared_state_t *state);
void wifi_state_machine_start_sta(wifi_shared_state_t *state);
void wifi_state_machine_process_transition(wifi_shared_state_t *state,
                                           wifi_state_transition_t transition,
                                           const void *event_data);
bool wifi_state_machine_sta_has_credentials(void);

#ifdef ESP_PLATFORM
esp_err_t wifi_state_machine_configure_sta(wifi_shared_state_t *state);
esp_err_t wifi_state_machine_start_fallback_ap(wifi_shared_state_t *state);
void wifi_state_machine_retry_timer_callback(TimerHandle_t timer);
esp_err_t wifi_state_machine_register_event_handlers(wifi_shared_state_t *state);
void wifi_state_machine_unregister_event_handlers(wifi_shared_state_t *state);
void wifi_state_machine_event_handler(void *ctx, esp_event_base_t base, int32_t id, void *event_data);
#endif
