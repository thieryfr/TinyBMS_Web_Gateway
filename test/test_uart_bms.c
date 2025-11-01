#include "unity.h"

#include "event_bus.h"
#include "uart_bms.h"

#include "app_events.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"

#include <string.h>

#include "uart_test_vectors.h"

static void reset_bus(void)
{
    event_bus_deinit();
    event_bus_init();
}

static bool s_listener_called = false;
static uart_bms_live_data_t s_listener_data;

static void test_listener(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    s_listener_called = true;
    if (data != NULL) {
        s_listener_data = *data;
    }
}

TEST_CASE("uart_bms_process_frame publishes event and notifies listeners", "[uart_bms]")
{
    reset_bus();
    s_listener_called = false;
    memset(&s_listener_data, 0, sizeof(s_listener_data));

    uart_bms_set_event_publisher(event_bus_get_publish_hook());

    event_bus_subscription_handle_t subscriber =
        event_bus_subscribe(2, NULL, NULL);
    TEST_ASSERT_NOT_NULL(subscriber);

    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_register_listener(test_listener, NULL));

    uint8_t frame[128] = {0};
    size_t frame_len = build_uart_test_frame(frame, sizeof(frame));
    TEST_ASSERT_NOT_EQUAL(0U, frame_len);

    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_process_frame(frame, frame_len));

    event_bus_event_t event = {0};
    TEST_ASSERT_TRUE(event_bus_receive(subscriber, &event, pdMS_TO_TICKS(50)));
    TEST_ASSERT_EQUAL(APP_EVENT_ID_BMS_LIVE_DATA, event.id);
    TEST_ASSERT_EQUAL(sizeof(uart_bms_live_data_t), event.payload_size);
    TEST_ASSERT_NOT_NULL(event.payload);

    const uart_bms_live_data_t *payload = (const uart_bms_live_data_t *)event.payload;
    TEST_ASSERT_EQUAL_UINT32(kUartTestRegisterCount, payload->register_count);
    TEST_ASSERT_EQUAL_UINT16(0x0020, payload->registers[0].address);
    TEST_ASSERT_EQUAL_UINT16(0x3456, payload->registers[0].raw_value);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 51.35f, payload->pack_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.3f, payload->pack_current_a);
    TEST_ASSERT_EQUAL_UINT16(3200, payload->min_cell_mv);
    TEST_ASSERT_EQUAL_UINT16(3320, payload->max_cell_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.64f, payload->state_of_charge_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 91.23f, payload->state_of_health_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.5f, payload->average_temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, payload->auxiliary_temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 27.5f, payload->mosfet_temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.0f, payload->pack_temperature_min_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 28.0f, payload->pack_temperature_max_c);
    TEST_ASSERT_EQUAL_UINT16(0x0003, payload->balancing_bits);
    TEST_ASSERT_EQUAL_UINT16(0x0091, payload->alarm_bits);
    TEST_ASSERT_EQUAL_UINT16(0x0002, payload->warning_bits);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)0x0012 << 16 | 0x3456, payload->uptime_seconds);
    TEST_ASSERT_EQUAL_UINT32(0, payload->cycle_count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 120.5f, payload->battery_capacity_ah);
    TEST_ASSERT_EQUAL_UINT16(16, payload->series_cell_count);
    TEST_ASSERT_EQUAL_UINT16(4200, payload->overvoltage_cutoff_mv);
    TEST_ASSERT_EQUAL_UINT16(3000, payload->undervoltage_cutoff_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 150.0f, payload->discharge_overcurrent_limit_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 63.0f, payload->charge_overcurrent_limit_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 120.0f, payload->peak_discharge_current_limit_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 62.0f, payload->overheat_cutoff_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -16.0f, payload->low_temp_charge_cutoff_c);
    TEST_ASSERT_EQUAL_UINT8(2, payload->hardware_version);
    TEST_ASSERT_EQUAL_UINT8(1, payload->hardware_changes_version);
    TEST_ASSERT_EQUAL_UINT8(0x34, payload->firmware_version);
    TEST_ASSERT_EQUAL_UINT8(0x12, payload->firmware_flags);
    TEST_ASSERT_EQUAL_UINT16(0x0456, payload->internal_firmware_version);

    TEST_ASSERT_TRUE(s_listener_called);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 51.35f, s_listener_data.pack_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.3f, s_listener_data.pack_current_a);

    uart_bms_unregister_listener(test_listener, NULL);
    event_bus_unsubscribe(subscriber);
    event_bus_deinit();
}

TEST_CASE("uart_bms_process_frame rejects invalid crc", "[uart_bms]")
{
    reset_bus();
    uart_bms_set_event_publisher(NULL);

    uint8_t frame[128] = {0};
    size_t frame_len = build_uart_test_frame(frame, sizeof(frame));
    TEST_ASSERT_NOT_EQUAL(0U, frame_len);

    frame[10] ^= 0xFF;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, uart_bms_process_frame(frame, frame_len));
}
