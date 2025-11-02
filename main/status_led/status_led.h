#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the status LED controller and start the background tasks.
 */
void status_led_init(void);

/**
 * @brief Notify the controller that the system completed its boot sequence.
 */
void status_led_notify_system_ready(void);

#ifdef __cplusplus
}
#endif

