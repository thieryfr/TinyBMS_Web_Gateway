#include "event_bus.h"

#include <inttypes.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

typedef struct event_bus_subscription {
    QueueHandle_t queue;
    event_bus_subscriber_cb_t callback;
    void *context;
    uint32_t dropped_events;
    struct event_bus_subscription *next;
} event_bus_subscription_t;

static event_bus_subscription_t *s_subscribers = NULL;
static SemaphoreHandle_t s_bus_lock = NULL;
static portMUX_TYPE s_init_spinlock = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "event_bus";

static bool event_bus_take_lock(void)
{
    if (s_bus_lock == NULL) {
        return false;
    }

    return xSemaphoreTake(s_bus_lock, portMAX_DELAY) == pdTRUE;
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

    if (xSemaphoreTake(s_bus_lock, portMAX_DELAY) != pdTRUE) {
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

event_bus_subscription_handle_t event_bus_subscribe(size_t queue_length,
                                                     event_bus_subscriber_cb_t callback,
                                                     void *context)
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
    subscription->next = NULL;

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

    bool success = true;
    event_bus_subscription_t *subscriber = s_subscribers;
    while (subscriber != NULL) {
        if (xQueueSend(subscriber->queue, event, timeout) != pdTRUE) {
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
        }
        subscriber = subscriber->next;
    }

    event_bus_give_lock();
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
    return true;
}

uint32_t event_bus_get_dropped_events(event_bus_subscription_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    return handle->dropped_events;
}
