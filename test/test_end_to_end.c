#include "unity.h"

#include "app_events.h"
#include "config_manager.h"
#include "event_bus.h"
#include "uart_bms.h"

#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <string.h>

#include "uart_test_vectors.h"

static void reset_bus(void)
{
    event_bus_deinit();
    event_bus_init();
}

static bool receive_event(event_bus_subscription_handle_t subscriber,
                          event_bus_event_t *event,
                          TickType_t timeout)
{
    if (event == NULL) {
        return false;
    }
    memset(event, 0, sizeof(*event));
    return event_bus_receive(subscriber, event, timeout);
}

TEST_CASE("end_to_end_uart_web_config_flow", "[integration]")
{
    reset_bus();
    uart_bms_set_event_publisher(event_bus_get_publish_hook());
    config_manager_set_event_publisher(event_bus_get_publish_hook());
    config_manager_init();

    event_bus_subscription_handle_t subscriber = event_bus_subscribe(8, NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    uint8_t frame[128] = {0};
    size_t frame_len = build_uart_test_frame(frame, sizeof(frame));
    TEST_ASSERT_NOT_EQUAL(0U, frame_len);

    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_process_frame(frame, frame_len));

    bool got_raw = false;
    bool got_decoded = false;
    bool got_live = false;
    const char *decoded_payload = NULL;
    const uart_bms_live_data_t *live_payload = NULL;

    for (int i = 0; i < 4; ++i) {
        event_bus_event_t event = {0};
        if (!receive_event(subscriber, &event, pdMS_TO_TICKS(50))) {
            break;
        }

        switch (event.id) {
        case APP_EVENT_ID_UART_FRAME_RAW:
            TEST_ASSERT_NOT_NULL(event.payload);
            const char *raw_payload = (const char *)event.payload;
            TEST_ASSERT_NOT_NULL(raw_payload);
            TEST_ASSERT_NOT_EQUAL(0, strstr(raw_payload, "\"type\":\"uart_raw\""));
            TEST_ASSERT_NOT_EQUAL(0, strstr(raw_payload, "\"timestamp_ms\":"));
            TEST_ASSERT_NOT_EQUAL(0, strstr(raw_payload, "\"timestamp\":"));
            got_raw = true;
            break;
        case APP_EVENT_ID_UART_FRAME_DECODED:
            TEST_ASSERT_NOT_NULL(event.payload);
            decoded_payload = (const char *)event.payload;
            TEST_ASSERT_NOT_NULL(decoded_payload);
            TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"timestamp_ms\":"));
            TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"timestamp\":"));
            got_decoded = true;
            break;
        case APP_EVENT_ID_BMS_LIVE_DATA:
            TEST_ASSERT_NOT_NULL(event.payload);
            live_payload = (const uart_bms_live_data_t *)event.payload;
            got_live = true;
            break;
        default:
            break;
        }
    }

    TEST_ASSERT_TRUE(got_raw);
    TEST_ASSERT_TRUE(got_decoded);
    TEST_ASSERT_TRUE(got_live);
    TEST_ASSERT_NOT_NULL(decoded_payload);
    TEST_ASSERT_NOT_NULL(live_payload);

    TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"pack_voltage\":51.350"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"state_of_charge\":75.64"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"registers\":[{"));

    TEST_ASSERT_EQUAL_UINT32(kUartTestRegisterCount, live_payload->register_count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 51.35f, live_payload->pack_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.3f, live_payload->pack_current_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.64f, live_payload->state_of_charge_pct);

    event_bus_event_t drain = {0};
    while (receive_event(subscriber, &drain, 0)) {
        /* Drain remaining events before sending configuration update. */
    }

    static const char kUpdateJson[] = "{\"key\":\"fully_charged_voltage_mv\",\"value\":3800}";
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_apply_register_update_json(kUpdateJson, 0));

    bool got_config_update = false;
    const char *config_payload = NULL;
    for (int i = 0; i < 4; ++i) {
        event_bus_event_t event = {0};
        if (!receive_event(subscriber, &event, pdMS_TO_TICKS(50))) {
            break;
        }
        if (event.id == APP_EVENT_ID_CONFIG_UPDATED) {
            TEST_ASSERT_NOT_NULL(event.payload);
            config_payload = (const char *)event.payload;
            got_config_update = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(got_config_update);
    TEST_ASSERT_NOT_NULL(config_payload);
    TEST_ASSERT_NOT_EQUAL(0, strstr(config_payload, "register_update"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(config_payload, "fully_charged_voltage_mv"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(config_payload, "\"raw\":3800"));

    char registers_json[CONFIG_MANAGER_MAX_REGISTERS_JSON] = {0};
    size_t registers_length = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      config_manager_get_registers_json(registers_json,
                                                         sizeof(registers_json),
                                                         &registers_length));
    if (registers_length < sizeof(registers_json)) {
        registers_json[registers_length] = '\0';
    }
    TEST_ASSERT_NOT_EQUAL(0, strstr(registers_json, "\"key\":\"fully_charged_voltage_mv\""));
    TEST_ASSERT_NOT_EQUAL(0, strstr(registers_json, "\"value\":3800"));

    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}
