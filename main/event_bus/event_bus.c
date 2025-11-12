#include "event_bus.h"

#include <inttypes.h>

#include "esp_log.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

typedef struct event_bus_subscription {
    QueueHandle_t queue;
    event_bus_subscriber_cb_t callback;
    void *context;
    uint32_t dropped_events;
    UBaseType_t queue_length;
    char name[CONFIG_TINYBMS_EVENT_BUS_NAME_MAX_LENGTH];
    struct event_bus_subscription *next;
} event_bus_subscription_t;

typedef struct {
    event_bus_payload_dispose_fn_t dispose;
    void *context;
    uint32_t refcount;
} event_bus_event_lifetime_t;

static event_bus_subscription_t *s_subscribers = NULL;
static SemaphoreHandle_t s_bus_lock = NULL;
static portMUX_TYPE s_init_spinlock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_lifetime_spinlock = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "event_bus";

static void event_bus_lifetime_retain(event_bus_event_lifetime_t *lifetime)
{
    if (lifetime == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_lifetime_spinlock);
    lifetime->refcount++;
    portEXIT_CRITICAL(&s_lifetime_spinlock);
}

static bool event_bus_lifetime_release(event_bus_event_lifetime_t *lifetime)
{
    if (lifetime == NULL) {
        return false;
    }

    bool should_dispose = false;

    portENTER_CRITICAL(&s_lifetime_spinlock);
    if (lifetime->refcount > 0U) {
        lifetime->refcount--;
        if (lifetime->refcount == 0U) {
            should_dispose = true;
        }
    }
    portEXIT_CRITICAL(&s_lifetime_spinlock);

    return should_dispose;
}

static void event_bus_lifetime_dispose(event_bus_event_lifetime_t *lifetime)
{
    if (lifetime == NULL) {
        return;
    }

    if (lifetime->dispose != NULL) {
        lifetime->dispose(lifetime->context);
    }
    vPortFree(lifetime);
}

// Timeout pour acquisition mutex (5 secondes - évite deadlock)
#define EVENT_BUS_MUTEX_TIMEOUT_MS 5000

static bool event_bus_take_lock(void)
{
    if (s_bus_lock == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_bus_lock, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire event bus lock (timeout)");
        return false;
    }

    return true;
}

static void event_bus_give_lock(void)
{
    if (s_bus_lock != NULL) {
        xSemaphoreGive(s_bus_lock);
    }
}

static void event_bus_ensure_init(void)
{
    if (s_bus_lock != NULL) {
        return;
    }

    portENTER_CRITICAL(&s_init_spinlock);
    if (s_bus_lock == NULL) {
        s_bus_lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_init_spinlock);
}

void event_bus_init(void)
{
    event_bus_ensure_init();
}

void event_bus_deinit(void)
{
    if (s_bus_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_bus_lock, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire lock for deinit (timeout)");
        return;
    }

    event_bus_subscription_t *iter = s_subscribers;
    s_subscribers = NULL;
    xSemaphoreGive(s_bus_lock);

    while (iter != NULL) {
        event_bus_subscription_t *next = iter->next;
        if (iter->queue != NULL) {
            vQueueDelete(iter->queue);
        }
        vPortFree(iter);
        iter = next;
    }

    SemaphoreHandle_t lock = s_bus_lock;
    s_bus_lock = NULL;
    vSemaphoreDelete(lock);
}

static event_bus_subscription_handle_t event_bus_subscribe_internal(size_t queue_length,
                                                                    event_bus_subscriber_cb_t callback,
                                                                    void *context,
                                                                    const char *name)
{
    if (queue_length == 0) {
        return NULL;
    }

    event_bus_ensure_init();
    if (s_bus_lock == NULL) {
        return NULL;
    }

    QueueHandle_t queue = xQueueCreate(queue_length, sizeof(event_bus_event_t));
    if (queue == NULL) {
        return NULL;
    }

    event_bus_subscription_t *subscription = pvPortMalloc(sizeof(event_bus_subscription_t));
    if (subscription == NULL) {
        vQueueDelete(queue);
        return NULL;
    }

    subscription->queue = queue;
    subscription->callback = callback;
    subscription->context = context;
    subscription->dropped_events = 0;
    subscription->queue_length = (UBaseType_t)queue_length;
    subscription->next = NULL;
    if (name != NULL) {
        strncpy(subscription->name, name, sizeof(subscription->name) - 1U);
        subscription->name[sizeof(subscription->name) - 1U] = '\0';
    } else {
        subscription->name[0] = '\0';
    }

    if (!event_bus_take_lock()) {
        vQueueDelete(queue);
        vPortFree(subscription);
        return NULL;
    }

    subscription->next = s_subscribers;
    s_subscribers = subscription;

    event_bus_give_lock();

    return subscription;
}

event_bus_subscription_handle_t event_bus_subscribe(size_t queue_length,
                                                     event_bus_subscriber_cb_t callback,
                                                     void *context)
{
    return event_bus_subscribe_internal(queue_length, callback, context, NULL);
}

event_bus_subscription_handle_t event_bus_subscribe_named(size_t queue_length,
                                                           const char *name,
                                                           event_bus_subscriber_cb_t callback,
                                                           void *context)
{
    return event_bus_subscribe_internal(queue_length, callback, context, name);
}

void event_bus_unsubscribe(event_bus_subscription_handle_t handle)
{
    if (handle == NULL || s_bus_lock == NULL) {
        return;
    }

    event_bus_subscription_t *to_free = NULL;

    if (!event_bus_take_lock()) {
        return;
    }

    event_bus_subscription_t **link = &s_subscribers;
    while (*link != NULL) {
        if (*link == handle) {
            *link = handle->next;
            to_free = handle;
            break;
        }
        link = &(*link)->next;
    }

    event_bus_give_lock();

    if (to_free == NULL) {
        return;
    }

    if (to_free->queue != NULL) {
        vQueueDelete(to_free->queue);
    }
    vPortFree(to_free);
}

bool event_bus_publish(const event_bus_event_t *event, TickType_t timeout)
{
    if (event == NULL || s_bus_lock == NULL) {
        return false;
    }

    if (!event_bus_take_lock()) {
        return false;
    }

    event_bus_event_lifetime_t *shared_lifetime = NULL;
    if (event->dispose != NULL) {
        shared_lifetime = pvPortMalloc(sizeof(event_bus_event_lifetime_t));
        if (shared_lifetime == NULL) {
            event_bus_give_lock();
            return false;
        }
        shared_lifetime->dispose = event->dispose;
        shared_lifetime->context = event->dispose_context;
        // Initialize refcount to 1 (publisher's reference)
        // This prevents premature disposal if a subscriber releases before we check
        shared_lifetime->refcount = 1U;
    }

    bool success = true;
    event_bus_subscription_t *subscriber = s_subscribers;
    while (subscriber != NULL) {
        event_bus_event_t queued = *event;
        queued.lifetime = shared_lifetime;
        if (xQueueSend(subscriber->queue, &queued, timeout) != pdTRUE) {
            success = false;
            subscriber->dropped_events++;

            // Log at power-of-2 milestones for visibility without flooding
            if ((subscriber->dropped_events & (subscriber->dropped_events - 1U)) == 0U) {
                // Critical threshold: escalate to ERROR level
                if (subscriber->dropped_events >= 256U) {
                    ESP_LOGE(TAG,
                             "CRITICAL: Subscriber %p queue saturated - event 0x%08" PRIx32 " dropped (%" PRIu32 " total drops). "
                             "Consumer may be stalled or queue undersized.",
                             (void *)subscriber,
                             (uint32_t)event->id,
                             subscriber->dropped_events);
                } else {
                    ESP_LOGW(TAG,
                             "Event 0x%08" PRIx32 " dropped for subscriber %p (%" PRIu32 " total drops) - queue full after timeout",
                             (uint32_t)event->id,
                             (void *)subscriber,
                             subscriber->dropped_events);
                }
            }
        } else {
            event_bus_lifetime_retain(shared_lifetime);
        }
        subscriber = subscriber->next;
    }

    event_bus_give_lock();

    // Release the publisher's reference
    if (shared_lifetime != NULL) {
        if (event_bus_lifetime_release(shared_lifetime)) {
            // If release returns true, we were the last holder - dispose now
            event_bus_lifetime_dispose(shared_lifetime);
        }
    }

    return success;
}

bool event_bus_receive(event_bus_subscription_handle_t handle,
                       event_bus_event_t *out_event,
                       TickType_t timeout)
{
    if (handle == NULL || out_event == NULL) {
        return false;
    }

    return xQueueReceive(handle->queue, out_event, timeout) == pdTRUE;
}

bool event_bus_dispatch(event_bus_subscription_handle_t handle, TickType_t timeout)
{
    if (handle == NULL || handle->callback == NULL) {
        return false;
    }

    event_bus_event_t event = {0};
    if (!event_bus_receive(handle, &event, timeout)) {
        return false;
    }

    handle->callback(&event, handle->context);
    event_bus_release(&event);
    return true;
}

uint32_t event_bus_get_dropped_events(event_bus_subscription_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    return handle->dropped_events;
}

size_t event_bus_get_all_metrics(event_bus_subscription_metrics_t *out_metrics, size_t capacity)
{
    if (out_metrics == NULL || capacity == 0 || s_bus_lock == NULL) {
        return 0;
    }

    if (!event_bus_take_lock()) {
        return 0;
    }

    size_t count = 0;
    event_bus_subscription_t *iter = s_subscribers;
    while (iter != NULL && count < capacity) {
        event_bus_subscription_metrics_t *dest = &out_metrics[count];
        strncpy(dest->name, iter->name, sizeof(dest->name) - 1U);
        dest->name[sizeof(dest->name) - 1U] = '\0';
        dest->queue_capacity = iter->queue_length;
        dest->dropped_events = iter->dropped_events;
        if (iter->queue != NULL) {
            dest->messages_waiting = (uint32_t)uxQueueMessagesWaiting(iter->queue);
        } else {
            dest->messages_waiting = 0;
        }
        ++count;
        iter = iter->next;
    }

    event_bus_give_lock();
    return count;
}

void event_bus_release(const event_bus_event_t *event)
{
    if (event == NULL) {
        return;
    }

    event_bus_event_lifetime_t *lifetime = (event_bus_event_lifetime_t *)event->lifetime;
    if (lifetime == NULL) {
        return;
    }

    if (event_bus_lifetime_release(lifetime)) {
        event_bus_lifetime_dispose(lifetime);
    }
}
