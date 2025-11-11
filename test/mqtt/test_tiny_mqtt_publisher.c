#include "unity.h"

#include "app_events.h"
#include "config_manager.h"
#include "tiny_mqtt_publisher.h"

#include "cJSON.h"

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

static cJSON *parse_json(const char *payload)
{
    if (payload == NULL) {
        return NULL;
    }
    return cJSON_Parse(payload);
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

    cJSON *root = parse_json(s_captured_payload);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *pack_voltage = cJSON_GetObjectItemCaseSensitive(root, "pack_voltage_v");
    TEST_ASSERT_TRUE(cJSON_IsNumber(pack_voltage));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 52.8f, (float)pack_voltage->valuedouble);

    const cJSON *power = cJSON_GetObjectItemCaseSensitive(root, "power_w");
    TEST_ASSERT_TRUE(cJSON_IsNumber(power));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -660.0f, (float)power->valuedouble);

    const cJSON *soc = cJSON_GetObjectItemCaseSensitive(root, "state_of_charge_pct");
    TEST_ASSERT_TRUE(cJSON_IsNumber(soc));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.5f, (float)soc->valuedouble);

    const cJSON *min_cell = cJSON_GetObjectItemCaseSensitive(root, "min_cell_voltage_v");
    TEST_ASSERT_TRUE(cJSON_IsNumber(min_cell));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.3f, (float)min_cell->valuedouble);

    const cJSON *cell_voltages = cJSON_GetObjectItemCaseSensitive(root, "cell_voltages_mv");
    TEST_ASSERT_TRUE(cJSON_IsArray(cell_voltages));
    TEST_ASSERT_EQUAL_INT(UART_BMS_CELL_COUNT, cJSON_GetArraySize(cell_voltages));

    const cJSON *alarms = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    TEST_ASSERT_TRUE(cJSON_IsObject(alarms));
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItemCaseSensitive(alarms, "high_charge")->valueint);
    TEST_ASSERT_EQUAL(2, cJSON_GetObjectItemCaseSensitive(alarms, "high_discharge")->valueint);

    const cJSON *limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
    TEST_ASSERT_TRUE(cJSON_IsObject(limits));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f,
                             (float)cJSON_GetObjectItemCaseSensitive(limits, "max_charge_current_a")->valuedouble);

    cJSON_Delete(root);
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

    cJSON *first_json = parse_json(first_payload);
    cJSON *second_json = parse_json(second_payload);
    cJSON *third_json = parse_json(third_payload);

    TEST_ASSERT_NOT_NULL(first_json);
    TEST_ASSERT_NOT_NULL(second_json);
    TEST_ASSERT_NOT_NULL(third_json);

    TEST_ASSERT_FLOAT_WITHIN(0.01f,
                             10.0f,
                             (float)cJSON_GetObjectItemCaseSensitive(first_json, "state_of_charge_pct")->valuedouble);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,
                             20.0f,
                             (float)cJSON_GetObjectItemCaseSensitive(second_json, "state_of_charge_pct")->valuedouble);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,
                             30.0f,
                             (float)cJSON_GetObjectItemCaseSensitive(third_json, "state_of_charge_pct")->valuedouble);

    cJSON_Delete(first_json);
    cJSON_Delete(second_json);
    cJSON_Delete(third_json);

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
    cJSON *root = parse_json(message.payload);
    TEST_ASSERT_NOT_NULL(root);
    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(root, "timestamp_ms");
    TEST_ASSERT_TRUE(cJSON_IsNumber(timestamp));
    TEST_ASSERT_EQUAL_DOUBLE(4200.0, timestamp->valuedouble);
    cJSON_Delete(root);
}

