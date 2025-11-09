#include "unity.h"

#include "event_bus.h"

#include "freertos/FreeRTOS.h"

#include <string.h>

static void reset_bus(void)
{
    event_bus_deinit();
    event_bus_init();
}

TEST_CASE("subscribe publish receive", "[event_bus]")
{
    reset_bus();

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe(2, NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    const char payload[] = "demo";
    const event_bus_event_t event = {
        .id = 0x01,
        .payload = payload,
        .payload_size = sizeof(payload),
    };

    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));

    event_bus_event_t received = {0};
    TEST_ASSERT_TRUE(event_bus_receive(subscriber, &received, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL(event.id, received.id);
    TEST_ASSERT_EQUAL(event.payload_size, received.payload_size);
    TEST_ASSERT_EQUAL_PTR(event.payload, received.payload);

    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}

static bool s_callback_called = false;
static event_bus_event_t s_callback_event;

static void test_callback(const event_bus_event_t *event, void *context)
{
    (void)context;
    s_callback_called = true;
    s_callback_event = *event;
}

TEST_CASE("dispatch invokes callback", "[event_bus]")
{
    reset_bus();
    s_callback_called = false;
    memset(&s_callback_event, 0, sizeof(s_callback_event));

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe(1, test_callback, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    const event_bus_event_t event = {
        .id = 0x42,
        .payload = NULL,
        .payload_size = 0,
    };

    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));
    TEST_ASSERT_TRUE(event_bus_dispatch(subscriber, pdMS_TO_TICKS(10)));

    TEST_ASSERT_TRUE(s_callback_called);
    TEST_ASSERT_EQUAL(event.id, s_callback_event.id);

    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}

TEST_CASE("queue full causes publish failure", "[event_bus]")
{
    reset_bus();

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe(1, NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    const event_bus_event_t event = {
        .id = 3,
        .payload = NULL,
        .payload_size = 0,
    };

    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));
    TEST_ASSERT_FALSE(event_bus_publish(&event, 0));

    event_bus_event_t received = {0};
    TEST_ASSERT_TRUE(event_bus_receive(subscriber, &received, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL(event.id, received.id);

    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}

TEST_CASE("unsubscribe stops further deliveries", "[event_bus]")
{
    reset_bus();

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe(1, NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    event_bus_unsubscribe(subscriber);

    const event_bus_event_t event = {
        .id = 7,
        .payload = NULL,
        .payload_size = 0,
    };

    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));
    event_bus_deinit();
}

TEST_CASE("metrics enumerate subscriptions", "[event_bus]")
{
    reset_bus();

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe_named(2, "metrics_test", NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    const event_bus_event_t event = {
        .id = 11,
        .payload = NULL,
        .payload_size = 0,
    };

    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));
    TEST_ASSERT_TRUE(event_bus_publish(&event, 0));
    TEST_ASSERT_FALSE(event_bus_publish(&event, 0));

    event_bus_subscription_metrics_t metrics[2] = {0};
    size_t count = event_bus_get_all_metrics(metrics, 2);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("metrics_test", metrics[0].name);
    TEST_ASSERT_EQUAL(2, metrics[0].queue_capacity);
    TEST_ASSERT_TRUE(metrics[0].messages_waiting <= metrics[0].queue_capacity);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, metrics[0].dropped_events);

    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}
