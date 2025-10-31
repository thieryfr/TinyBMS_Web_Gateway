#pragma once

#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the event bus publishing hook used by the Wi-Fi module.
 */
void wifi_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Initialise the Wi-Fi subsystem according to the project configuration.
 *
 * The function is safe to call multiple times and can be invoked even when
 * Wi-Fi support is disabled in the project configuration. In such cases it will
 * simply log the condition and return.
 */
void wifi_init(void);

#ifdef __cplusplus
}
#endif

