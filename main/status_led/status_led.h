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

/**
 * @brief Deinitialize the status LED controller and free resources.
 *
 * Stops background tasks, frees the command queue, and unsubscribes from events.
 */
void status_led_deinit(void);

#ifdef __cplusplus
}
#endif

