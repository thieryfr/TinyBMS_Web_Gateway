#pragma once

#include "app_events.h"
#include "wifi_state.h"

/**
 * @brief Register a new event publisher used for Wi-Fi events.
 */
void wifi_events_set_publisher(wifi_shared_state_t *state, event_bus_publish_fn_t publisher);

/**
 * @brief Publish a Wi-Fi event on the application event bus when available.
 */
void wifi_events_publish(wifi_shared_state_t *state, app_event_id_t id);
