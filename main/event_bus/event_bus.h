#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identifier describing the semantic of an event carried by the bus.
 */
typedef uint32_t event_bus_event_id_t;

/**
 * @brief Structure copied into the subscriber queue for each published event.
 *
 * The payload pointer is not owned by the event bus. Publishers must guarantee
 * that the pointed data remains valid until all subscribers have consumed the
 * message or copy the data before publishing.
 */
typedef struct {
    event_bus_event_id_t id;   /**< Application specific event identifier. */
    const void *payload;       /**< Optional pointer to the event payload. */
    size_t payload_size;       /**< Size of the payload in bytes. */
} event_bus_event_t;

struct event_bus_subscription;
typedef struct event_bus_subscription *event_bus_subscription_handle_t;

/**
 * @brief Signature of callback invoked when dispatching events.
 */
typedef void (*event_bus_subscriber_cb_t)(const event_bus_event_t *event, void *context);

/**
 * @brief Signature of the publishing hook exposed to other modules.
 */
typedef bool (*event_bus_publish_fn_t)(const event_bus_event_t *event, TickType_t timeout);

#ifndef CONFIG_TINYBMS_EVENT_BUS_DEFAULT_QUEUE_LENGTH
#define CONFIG_TINYBMS_EVENT_BUS_DEFAULT_QUEUE_LENGTH 32
#endif

/**
 * @brief Initialise the event bus infrastructure.
 *
 * The function is safe to call multiple times and will lazily create the
 * required synchronisation primitives.
 */
void event_bus_init(void);

/**
 * @brief Release all resources owned by the event bus.
 *
 * All active subscriptions are removed. Pending events in subscriber queues are
 * discarded.
 */
void event_bus_deinit(void);

/**
 * @brief Create a subscription with its own receive queue.
 *
 * @param queue_length  Number of events that can be queued for the subscriber
 *                      before publishers start failing.
 * @param callback      Optional callback invoked when using
 *                      ::event_bus_dispatch.
 * @param context       Opaque pointer passed to the callback.
 *
 * @return Handle to the subscription on success, NULL otherwise.
 */
event_bus_subscription_handle_t event_bus_subscribe(size_t queue_length,
                                                     event_bus_subscriber_cb_t callback,
                                                     void *context);

static inline event_bus_subscription_handle_t event_bus_subscribe_default(event_bus_subscriber_cb_t callback,
                                                                          void *context)
{
    return event_bus_subscribe(CONFIG_TINYBMS_EVENT_BUS_DEFAULT_QUEUE_LENGTH, callback, context);
}

/**
 * @brief Remove a subscription from the bus and free its resources.
 */
void event_bus_unsubscribe(event_bus_subscription_handle_t handle);

/**
 * @brief Publish an event to every active subscriber.
 *
 * The call is thread-safe and can be invoked from any FreeRTOS task.
 *
 * @param event     Pointer to the event description to enqueue for subscribers.
 * @param timeout   Maximum time to wait when a subscriber queue is full.
 *                  - 0: Non-blocking, event is dropped immediately if queue is full
 *                  - portMAX_DELAY: Wait indefinitely
 *                  - Other value: Maximum ticks to wait for queue space
 *
 * @return true when all subscribers accepted the event, false otherwise. When
 *         false is returned, at least one subscriber queue was full and the
 *         event was discarded for that subscriber after the timeout expired.
 */
bool event_bus_publish(const event_bus_event_t *event, TickType_t timeout);

/**
 * @brief Convenience function to access the canonical publisher implementation.
 */
static inline event_bus_publish_fn_t event_bus_get_publish_hook(void)
{
    return event_bus_publish;
}

/**
 * @brief Receive the next event for a given subscription.
 *
 * @param handle    Subscription handle returned by ::event_bus_subscribe.
 * @param out_event Pointer where the received event will be copied.
 * @param timeout   Maximum time to wait for an event.
 *
 * @return true when an event was received, false when the timeout expired.
 */
bool event_bus_receive(event_bus_subscription_handle_t handle,
                       event_bus_event_t *out_event,
                       TickType_t timeout);

/**
 * @brief Blocking helper combining ::event_bus_receive and the registered callback.
 *
 * @param handle  Subscription handle created with ::event_bus_subscribe.
 * @param timeout Maximum time to wait for an event before returning.
 *
 * @return true if the callback was invoked, false on timeout or when no
 *         callback was registered.
 */
bool event_bus_dispatch(event_bus_subscription_handle_t handle, TickType_t timeout);

/**
 * @brief Get the number of events dropped for a specific subscriber.
 *
 * This function allows monitoring of queue saturation. A non-zero value
 * indicates that the subscriber's queue was full and events were lost.
 *
 * @param handle Subscription handle created with ::event_bus_subscribe.
 *
 * @return Number of events that have been dropped for this subscriber since
 *         subscription creation, or 0 if the handle is invalid.
 */
uint32_t event_bus_get_dropped_events(event_bus_subscription_handle_t handle);

#ifdef __cplusplus
}
#endif
