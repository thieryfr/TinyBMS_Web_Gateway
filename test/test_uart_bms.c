#include "unity.h"

#include "event_bus.h"
#include "uart_bms.h"

#include "app_events.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"

#include <string.h>

static const uint16_t kRegisterCount = UART_BMS_REGISTER_WORD_COUNT;

static const uint16_t kSampleValues[UART_BMS_REGISTER_WORD_COUNT] = {
    0x3456, 0x0012, 0x6666, 0x424D, 0xCCCD, 0xC144, 0x0C80, 0x0CF8, 0x00F5,
    0x012C, 0xB22F, 0x2CC0, 0x0482, 0x0113, 0x0091, 0x0002, 0x0003, 0x1C12,
    0x0078, 0x2F12, 0x0010, 0x1068, 0x0BB8, 0x0096, 0x003F, 0x003E, 0x0102,
    0x1234, 0x0456,
};

static uint16_t compute_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static size_t build_frame(uint8_t *frame, size_t frame_size, const uint16_t *values, size_t value_count)
{
    size_t payload_len = value_count * sizeof(uint16_t);
    size_t total_len = payload_len + 5;
    TEST_ASSERT_TRUE(total_len <= frame_size);

    frame[0] = 0xAA;
    frame[1] = 0x09;
    frame[2] = (uint8_t)payload_len;

    for (size_t i = 0; i < value_count; ++i) {
        frame[3 + i * 2] = (uint8_t)(values[i] & 0xFF);
        frame[4 + i * 2] = (uint8_t)(values[i] >> 8);
    }

    uint16_t crc = compute_crc16(frame, total_len - 2);
    frame[total_len - 2] = (uint8_t)(crc & 0xFF);
    frame[total_len - 1] = (uint8_t)(crc >> 8);

    return total_len;
}

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
    size_t frame_len = build_frame(frame, sizeof(frame), kSampleValues, kRegisterCount);

    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_process_frame(frame, frame_len));

    event_bus_event_t event = {0};
    TEST_ASSERT_TRUE(event_bus_receive(subscriber, &event, pdMS_TO_TICKS(50)));
    TEST_ASSERT_EQUAL(APP_EVENT_ID_BMS_LIVE_DATA, event.id);
    TEST_ASSERT_EQUAL(sizeof(uart_bms_live_data_t), event.payload_size);
    TEST_ASSERT_NOT_NULL(event.payload);

    const uart_bms_live_data_t *payload = (const uart_bms_live_data_t *)event.payload;
    TEST_ASSERT_EQUAL_UINT32(kRegisterCount, payload->register_count);
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
    size_t frame_len = build_frame(frame, sizeof(frame), kSampleValues, kRegisterCount);

    frame[10] ^= 0xFF;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, uart_bms_process_frame(frame, frame_len));
}
