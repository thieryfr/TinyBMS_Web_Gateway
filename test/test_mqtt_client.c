#include "unity.h"

#include "mqtt_client.h"

#include <string.h>

static bool s_event_publisher_called = false;

static bool test_event_publisher(const event_bus_event_t *event, TickType_t timeout)
{
    (void)event;
    (void)timeout;
    s_event_publisher_called = true;
    return true;
}

static void reset_event_publisher(void)
{
    s_event_publisher_called = false;
}

static void sample_listener(const mqtt_client_event_t *event, void *context)
{
    (void)event;
    (void)context;
}

TEST_CASE("mqtt_client_operations_require_initialisation", "[mqtt][mqtt_client]")
{
    mqtt_client_state_t state;
    mqtt_client_get_state(&state);
    TEST_ASSERT_FALSE(state.initialised);
    TEST_ASSERT_FALSE(state.started);
    TEST_ASSERT_FALSE(state.client_handle_created);
    TEST_ASSERT_FALSE(state.event_publisher_registered);

    mqtt_client_config_t config = {
        .broker_uri = "mqtt://localhost",
        .keepalive_seconds = 30,
        .default_qos = 1,
        .retain_enabled = false,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mqtt_client_apply_configuration(&config));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mqtt_client_start());
    TEST_ASSERT_FALSE(mqtt_client_publish("demo/topic", "payload", strlen("payload"), 0, false, 0));
}

TEST_CASE("mqtt_client_init_records_listener", "[mqtt][mqtt_client]")
{
    const mqtt_client_event_listener_t listener = {
        .callback = sample_listener,
        .context = NULL,
    };

    TEST_ASSERT_EQUAL(ESP_OK, mqtt_client_init(&listener));

    mqtt_client_state_t state;
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.lock_created);
    TEST_ASSERT_TRUE(state.initialised);
    TEST_ASSERT_TRUE(state.listener_registered);
    TEST_ASSERT_FALSE(state.started);
    TEST_ASSERT_FALSE(state.client_handle_created);

}

TEST_CASE("mqtt_client_start_stop_transitions_state", "[mqtt][mqtt_client]")
{
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_client_start());
    mqtt_client_state_t state;
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.started);
    TEST_ASSERT_FALSE(state.client_handle_created);

    TEST_ASSERT_EQUAL(ESP_OK, mqtt_client_start());
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.started);

    mqtt_client_stop();
    mqtt_client_get_state(&state);
    TEST_ASSERT_FALSE(state.started);
}

TEST_CASE("mqtt_client_configuration_and_publish_behaviour", "[mqtt][mqtt_client]")
{
    mqtt_client_config_t config = {0};
    strncpy(config.broker_uri, "mqtt://example.com", sizeof(config.broker_uri) - 1U);
    strncpy(config.username, "demo", sizeof(config.username) - 1U);
    strncpy(config.password, "secret", sizeof(config.password) - 1U);
    config.keepalive_seconds = 45U;
    config.default_qos = 1U;
    config.retain_enabled = true;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mqtt_client_apply_configuration(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_client_apply_configuration(&config));

    mqtt_client_state_t state;
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.initialised);
    TEST_ASSERT_FALSE(state.client_handle_created);

    TEST_ASSERT_FALSE(mqtt_client_publish("demo/topic", "payload", strlen("payload"), 1, true, pdMS_TO_TICKS(10)));

    TEST_ASSERT_EQUAL(ESP_OK, mqtt_client_start());
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.started);

    TEST_ASSERT_FALSE(mqtt_client_publish("demo/topic", "payload", strlen("payload"), 1, true, pdMS_TO_TICKS(10)));

    mqtt_client_stop();
}

TEST_CASE("mqtt_client_event_publisher_registration", "[mqtt][mqtt_client]")
{
    reset_event_publisher();
    mqtt_client_set_event_publisher(test_event_publisher);

    mqtt_client_state_t state;
    mqtt_client_get_state(&state);
    TEST_ASSERT_TRUE(state.event_publisher_registered);
    TEST_ASSERT_TRUE(state.lock_created);
    TEST_ASSERT_FALSE(state.client_handle_created);

    TEST_ASSERT_TRUE(state.event_publisher_registered);
    TEST_ASSERT_FALSE(s_event_publisher_called);
    (void)test_event_publisher(NULL, 0);
    TEST_ASSERT_TRUE(s_event_publisher_called);
}
