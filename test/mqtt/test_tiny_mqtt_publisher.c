#include "unity.h"

#include "app_events.h"
#include "tiny_mqtt_publisher.h"

#include <string.h>

static bool s_publish_called = false;
static unsigned s_publish_count = 0U;
static tiny_mqtt_publisher_message_t s_captured_message = {0};
static char s_captured_payload[512];

static void reset_capture(void)
{
    s_publish_called = false;
    s_publish_count = 0U;
    memset(&s_captured_message, 0, sizeof(s_captured_message));
    memset(s_captured_payload, 0, sizeof(s_captured_payload));
}

static void clear_capture_flags(void)
{
    s_publish_called = false;
    memset(&s_captured_message, 0, sizeof(s_captured_message));
    memset(s_captured_payload, 0, sizeof(s_captured_payload));
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
        memcpy(s_captured_payload, message->payload, message->payload_length);
        s_captured_payload[message->payload_length] = '\0';
        s_captured_message = *message;
        s_captured_message.payload = s_captured_payload;
        s_publish_called = true;
    }
    ++s_publish_count;
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
    return data;
}

TEST_CASE("tiny_mqtt_publisher_generates_metrics_snapshot", "[mqtt][tiny_mqtt_publisher]")
{
    tiny_mqtt_publisher_set_event_publisher(capture_event);
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
    TEST_ASSERT_NOT_NULL(s_captured_message.payload);
    TEST_ASSERT_GREATER_THAN(0U, s_captured_message.payload_length);

    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"pack_voltage_v\":52.800"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"power_w\":-660.000"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"state_of_charge_pct\":75.50"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"average_temperature_c\":32.200"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"min_cell_voltage_v\":3.300"));
    TEST_ASSERT_NOT_NULL(strstr(s_captured_payload, "\"balancing_bits\":3"));
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

