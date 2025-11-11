#include "wifi_state.h"

void wifi_state_reset(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->initialised = false;
    state->ap_fallback_active = false;
    state->retry_count = 0;
#ifdef ESP_PLATFORM
    state->sta_netif = NULL;
    state->ap_netif = NULL;
    state->wifi_event_handle = NULL;
    state->ip_got_handle = NULL;
    state->ip_lost_handle = NULL;
    state->sta_retry_timer = NULL;
    state->mutex = NULL;
#endif
}

void wifi_state_clear_publisher(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->publisher = NULL;
}
