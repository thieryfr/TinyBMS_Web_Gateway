#include "unity.h"

#include "app_events.h"
#include "config_manager.h"
#include "tiny_mqtt_publisher.h"

#include <string.h>

static bool s_publish_called = false;
static unsigned s_publish_count = 0U;
static tiny_mqtt_publisher_message_t s_captured_message = {0};
static char s_captured_payload[TINY_MQTT_MAX_PAYLOAD_SIZE];
static char s_captured_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];

typedef struct {
    const tiny_mqtt_publisher_message_t *message;
    const char *payload_ptr;
    event_bus_payload_dispose_fn_t dispose;
    void *dispose_context;
} held_event_t;

static held_event_t s_held_events[8];
static size_t s_held_event_count = 0U;

static void release_held_events(void)
{
    for (size_t i = 0; i < s_held_event_count; ++i) {
        if (s_held_events[i].dispose != NULL) {
            s_held_events[i].dispose(s_held_events[i].dispose_context);
        }
        s_held_events[i].message = NULL;
        s_held_events[i].payload_ptr = NULL;
        s_held_events[i].dispose = NULL;
        s_held_events[i].dispose_context = NULL;
    }
    s_held_event_count = 0U;
}

static void reset_capture(void)
{
    s_publish_called = false;
    s_publish_count = 0U;
    memset(&s_captured_message, 0, sizeof(s_captured_message));
    memset(s_captured_payload, 0, sizeof(s_captured_payload));
    memset(s_captured_topic, 0, sizeof(s_captured_topic));
    release_held_events();
}

static void clear_capture_flags(void)
{
    s_publish_called = false;
    memset(&s_captured_message, 0, sizeof(s_captured_message));
    memset(s_captured_payload, 0, sizeof(s_captured_payload));
    memset(s_captured_topic, 0, sizeof(s_captured_topic));
}

static bool capture_event(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;
    if (event == NULL) {
        return false;
    }
    if (event->id != APP_EVENT_ID_MQTT_METRICS) {
        return true;
    }

    const tiny_mqtt_publisher_message_t *message =
        (const tiny_mqtt_publisher_message_t *)event->payload;
    if (message != NULL && message->payload != NULL && message->payload_length < sizeof(s_captured_payload)) {
        if (message->topic != NULL && message->topic_length < sizeof(s_captured_topic)) {
            memcpy(s_captured_topic, message->topic, message->topic_length);
            s_captured_topic[message->topic_length] = '\0';
        }
        memcpy(s_captured_payload, message->payload, message->payload_length);
        s_captured_payload[message->payload_length] = '\0';
        s_captured_message = *message;
        s_captured_message.payload = s_captured_payload;
        s_captured_message.topic = s_captured_topic;
        s_publish_called = true;
    }
    ++s_publish_count;
    if (event->dispose != NULL) {
        event->dispose(event->dispose_context);
    }
    return true;
}

static bool capture_and_hold_event(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;
    if (event == NULL) {
        return false;
    }
    if (event->id != APP_EVENT_ID_MQTT_METRICS) {
        if (event->dispose != NULL) {
            event->dispose(event->dispose_context);
        }
        return true;
    }

    if (s_held_event_count >= (sizeof(s_held_events) / sizeof(s_held_events[0]))) {
        if (event->dispose != NULL) {
            event->dispose(event->dispose_context);
        }
        return false;
    }

    const tiny_mqtt_publisher_message_t *message =
        (const tiny_mqtt_publisher_message_t *)event->payload;
    if (message == NULL) {
        if (event->dispose != NULL) {
            event->dispose(event->dispose_context);
        }
        return false;
    }

    held_event_t *slot = &s_held_events[s_held_event_count++];
    slot->message = message;
    slot->payload_ptr = message->payload;
    slot->dispose = event->dispose;
    slot->dispose_context = event->dispose_context;

    return true;
}

static uart_bms_live_data_t build_sample(uint64_t timestamp_ms)
{
    uart_bms_live_data_t data = {0};
    data.timestamp_ms = timestamp_ms;
    data.uptime_seconds = 42U;
    data.cycle_count = 3U;
    data.pack_voltage_v = 52.8f;
    data.pack_current_a = -12.5f;
    data.state_of_charge_pct = 75.5f;
    data.state_of_health_pct = 98.7f;
    data.average_temperature_c = 32.2f;
    data.mosfet_temperature_c = 35.5f;
    data.min_cell_mv = 3300U;
    data.max_cell_mv = 3400U;
    data.balancing_bits = 0x0003U;
    data.alarm_bits = 0x1234U;
    data.warning_bits = 0x00FFU;
    data.max_charge_current_limit_a = 40.0f;
    data.max_discharge_current_limit_a = 60.0f;
    data.charge_overcurrent_limit_a = 38.0f;
    data.discharge_overcurrent_limit_a = 10.0f;
    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        data.cell_voltage_mv[i] = (uint16_t)(3300U + (uint16_t)(i * 10U));
        data.cell_balancing[i] = (uint8_t)((i % 2U) == 0U ? 1U : 0U);
    }
    return data;
}

TEST_CASE("tiny_mqtt_publisher_generates_metrics_snapshot", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_event_publisher(capture_event);
    tiny_mqtt_publisher_set_metrics_topic("test/device/metrics");
    tiny_mqtt_publisher_reset();
    reset_capture();

    tiny_mqtt_publisher_config_t cfg = {
        .publish_interval_ms = 0U,
        .qos = 1,
        .retain = false,
    };
    tiny_mqtt_publisher_apply_config(&cfg);

    uart_bms_live_data_t sample = build_sample(1000U);
    tiny_mqtt_publisher_on_bms_update(&sample, NULL);

    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_EQUAL(1, s_captured_message.qos);
    TEST_ASSERT_FALSE(s_captured_message.retain);
    TEST_ASSERT_EQUAL_STRING("test/device/metrics", s_captured_message.topic);
    TEST_ASSERT_NOT_NULL(s_captured_message.payload);
    TEST_ASSERT_GREATER_THAN(0U, s_captured_message.payload_length);

    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"pack_voltage_v\":52.800"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"power_w\":-660.000"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"state_of_charge_pct\":75.50"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"average_temperature_c\":32.200"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"min_cell_voltage_v\":3.300"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"balancing_bits\":3"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"cell_voltages_mv\":[3300,3310,3320"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"cell_balancing\":[1,0,1"));
    TEST_ASSERT_NOT_NULL(
        strstr(s_captured_payload,
               "\"alarms\":{\"high_charge\":0,\"high_discharge\":2,\"cell_imbalance\":2,\"raw_alarm_bits\":4660,\"raw_warning_bits\":255}"));
    TEST_ASSERT_NOT_NULL(
        strstr(s_captured_payload,
               "\"limits\":{\"max_charge_current_a\":40.00,\"max_discharge_current_a\":60.00,\"charge_overcurrent_limit_a\":38.00,\"discharge_overcurrent_limit_a\":10.00}"));
}

TEST_CASE("tiny_mqtt_publisher_respects_publish_interval", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_event_publisher(capture_event);
    tiny_mqtt_publisher_set_metrics_topic("test/device/metrics");
    tiny_mqtt_publisher_reset();
    reset_capture();

    tiny_mqtt_publisher_config_t cfg = {
        .publish_interval_ms = 1000U,
        .qos = 0,
        .retain = false,
    };
    tiny_mqtt_publisher_apply_config(&cfg);

    uart_bms_live_data_t first = build_sample(1000U);
    tiny_mqtt_publisher_on_bms_update(&first, NULL);
    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_EQUAL(1U, s_publish_count);

    clear_capture_flags();
    uart_bms_live_data_t second = build_sample(1500U);
    tiny_mqtt_publisher_on_bms_update(&second, NULL);
    TEST_ASSERT_FALSE(s_publish_called);
    TEST_ASSERT_EQUAL(1U, s_publish_count);

    clear_capture_flags();
    uart_bms_live_data_t third = build_sample(2200U);
    tiny_mqtt_publisher_on_bms_update(&third, NULL);
    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_EQUAL(2U, s_publish_count);
}

TEST_CASE("tiny_mqtt_publisher_keeps_interval_when_updating_qos", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_event_publisher(capture_event);
    tiny_mqtt_publisher_set_metrics_topic("test/device/metrics");
    tiny_mqtt_publisher_reset();
    reset_capture();

    tiny_mqtt_publisher_config_t cfg = {
        .publish_interval_ms = 800U,
        .qos = 0,
        .retain = false,
    };
    tiny_mqtt_publisher_apply_config(&cfg);

    uart_bms_live_data_t first = build_sample(800U);
    tiny_mqtt_publisher_on_bms_update(&first, NULL);
    TEST_ASSERT_TRUE(s_publish_called);

    clear_capture_flags();
    tiny_mqtt_publisher_config_t update = {
        .publish_interval_ms = TINY_MQTT_PUBLISH_INTERVAL_KEEP,
        .qos = 2,
        .retain = true,
    };
    tiny_mqtt_publisher_apply_config(&update);

    uart_bms_live_data_t second = build_sample(1200U);
    tiny_mqtt_publisher_on_bms_update(&second, NULL);
    TEST_ASSERT_FALSE(s_publish_called);

    clear_capture_flags();
    uart_bms_live_data_t third = build_sample(1700U);
    tiny_mqtt_publisher_on_bms_update(&third, NULL);
    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_EQUAL(2, s_captured_message.qos);
    TEST_ASSERT_TRUE(s_captured_message.retain);
}

TEST_CASE("tiny_mqtt_publisher_provides_unique_buffers_for_pending_events", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_event_publisher(capture_and_hold_event);
    tiny_mqtt_publisher_set_metrics_topic("unique/topic");
    tiny_mqtt_publisher_reset();
    reset_capture();

    tiny_mqtt_publisher_config_t cfg = {
        .publish_interval_ms = 0U,
        .qos = 1,
        .retain = false,
    };
    tiny_mqtt_publisher_apply_config(&cfg);

    uart_bms_live_data_t first = build_sample(1000U);
    first.state_of_charge_pct = 10.0f;
    uart_bms_live_data_t second = build_sample(1100U);
    second.state_of_charge_pct = 20.0f;
    uart_bms_live_data_t third = build_sample(1200U);
    third.state_of_charge_pct = 30.0f;

    tiny_mqtt_publisher_on_bms_update(&first, NULL);
    tiny_mqtt_publisher_on_bms_update(&second, NULL);
    tiny_mqtt_publisher_on_bms_update(&third, NULL);

    TEST_ASSERT_EQUAL_UINT32(3U, s_held_event_count);
    TEST_ASSERT_NOT_NULL(s_held_events[0].message);
    TEST_ASSERT_NOT_NULL(s_held_events[1].message);
    TEST_ASSERT_NOT_NULL(s_held_events[2].message);

    TEST_ASSERT_NOT_NULL(s_held_events[0].payload_ptr);
    TEST_ASSERT_NOT_NULL(s_held_events[1].payload_ptr);
    TEST_ASSERT_NOT_NULL(s_held_events[2].payload_ptr);

    TEST_ASSERT_NOT_EQUAL(s_held_events[0].payload_ptr, s_held_events[1].payload_ptr);
    TEST_ASSERT_NOT_EQUAL(s_held_events[0].payload_ptr, s_held_events[2].payload_ptr);
    TEST_ASSERT_NOT_EQUAL(s_held_events[1].payload_ptr, s_held_events[2].payload_ptr);

    char first_payload[TINY_MQTT_MAX_PAYLOAD_SIZE];
    char second_payload[TINY_MQTT_MAX_PAYLOAD_SIZE];
    char third_payload[TINY_MQTT_MAX_PAYLOAD_SIZE];

    size_t len0 = s_held_events[0].message->payload_length;
    if (len0 >= sizeof(first_payload)) {
        len0 = sizeof(first_payload) - 1U;
    }
    memcpy(first_payload, s_held_events[0].payload_ptr, len0);
    first_payload[len0] = '\0';

    size_t len1 = s_held_events[1].message->payload_length;
    if (len1 >= sizeof(second_payload)) {
        len1 = sizeof(second_payload) - 1U;
    }
    memcpy(second_payload, s_held_events[1].payload_ptr, len1);
    second_payload[len1] = '\0';

    size_t len2 = s_held_events[2].message->payload_length;
    if (len2 >= sizeof(third_payload)) {
        len2 = sizeof(third_payload) - 1U;
    }
    memcpy(third_payload, s_held_events[2].payload_ptr, len2);
    third_payload[len2] = '\0';

    TEST_ASSERT_NOT_NULL(strstr(first_payload, "\"state_of_charge_pct\":10.00"));
    TEST_ASSERT_NOT_NULL(strstr(second_payload, "\"state_of_charge_pct\":20.00"));
    TEST_ASSERT_NOT_NULL(strstr(third_payload, "\"state_of_charge_pct\":30.00"));

    release_held_events();
}

TEST_CASE("tiny_mqtt_publisher_build_metrics_message_helper", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_metrics_topic("helper/topic");
    tiny_mqtt_publisher_reset();

    uart_bms_live_data_t sample = build_sample(4200U);

    tiny_mqtt_publisher_message_t message = {0};
    bool ok = tiny_mqtt_publisher_build_metrics_message(&sample, &message);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(message.topic);
    TEST_ASSERT_EQUAL_STRING("helper/topic", message.topic);
    TEST_ASSERT_NOT_NULL(message.payload);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, message.payload_length);
    TEST_ASSERT_LESS_THAN_UINT32(TINY_MQTT_MAX_PAYLOAD_SIZE, message.payload_length);
    TEST_ASSERT_NOT_NULL(strstr(message.payload, "\"timestamp_ms\":4200"));
}

