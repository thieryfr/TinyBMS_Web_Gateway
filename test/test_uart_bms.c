#include "unity.h"

#include "uart_bms.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

static uint16_t compute_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void build_frame(const uint16_t *values, size_t value_count, uint8_t *out_frame, size_t *out_length)
{
    size_t payload_len = value_count * sizeof(uint16_t);
    size_t frame_len = 3 + payload_len + 2;

    out_frame[0] = 0xAA;
    out_frame[1] = 0x09;
    out_frame[2] = (uint8_t)payload_len;

    for (size_t i = 0; i < value_count; ++i) {
        out_frame[3 + i * 2] = (uint8_t)(values[i] & 0xFF);
        out_frame[3 + i * 2 + 1] = (uint8_t)(values[i] >> 8);
    }

    uint16_t crc = compute_crc(out_frame, frame_len - 2);
    out_frame[frame_len - 2] = (uint8_t)(crc & 0xFF);
    out_frame[frame_len - 1] = (uint8_t)(crc >> 8);

    if (out_length != NULL) {
        *out_length = frame_len;
    }
}

TEST_CASE("decode frame populates telemetry", "[uart_bms]")
{
    uint16_t values[25] = {0};
    values[0] = 5321;            // 53.21 V
    values[1] = (uint16_t)(int16_t)(-123); // -12.3 A
    values[2] = 3150;            // 3.150 V -> 3150 mV
    values[3] = 4200;            // 4.200 V -> 4200 mV
    values[4] = 7456;            // 74.56 %
    values[5] = 9500;            // 95.00 %
    values[6] = 255;             // 25.5 °C
    values[7] = (uint16_t)(int16_t)(-30);   // -3.0 °C
    values[8] = 0x0003;
    values[9] = 0x1000;
    values[10] = 0x0001;
    values[21] = 0x3456;
    values[22] = 0x0012;
    values[23] = 0x00AA;
    values[24] = 0x0003;

    uint8_t frame[3 + sizeof(values) + 2] = {0};
    size_t frame_len = 0;
    build_frame(values, 25, frame, &frame_len);

    uart_bms_live_data_t data = {0};
    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_decode_frame(frame, frame_len, &data));

    TEST_ASSERT_TRUE(data.pack_voltage_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 53.21f, data.pack_voltage_v);

    TEST_ASSERT_TRUE(data.pack_current_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.3f, data.pack_current_a);

    TEST_ASSERT_TRUE(data.min_cell_mv_valid);
    TEST_ASSERT_EQUAL_UINT16(3150, data.min_cell_mv);

    TEST_ASSERT_TRUE(data.max_cell_mv_valid);
    TEST_ASSERT_EQUAL_UINT16(4200, data.max_cell_mv);

    TEST_ASSERT_TRUE(data.state_of_charge_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 74.56f, data.state_of_charge_pct);

    TEST_ASSERT_TRUE(data.state_of_health_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 95.0f, data.state_of_health_pct);

    TEST_ASSERT_TRUE(data.average_temperature_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.5f, data.average_temperature_c);

    TEST_ASSERT_TRUE(data.mosfet_temperature_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -3.0f, data.mosfet_temperature_c);

    TEST_ASSERT_TRUE(data.balancing_bits_valid);
    TEST_ASSERT_EQUAL_UINT16(0x0003, data.balancing_bits);

    TEST_ASSERT_TRUE(data.alarm_bits_valid);
    TEST_ASSERT_EQUAL_UINT16(0x1000, data.alarm_bits);

    TEST_ASSERT_TRUE(data.warning_bits_valid);
    TEST_ASSERT_EQUAL_UINT16(0x0001, data.warning_bits);

    TEST_ASSERT_TRUE(data.uptime_valid);
    TEST_ASSERT_EQUAL_UINT32(0x00123456, data.uptime_seconds);

    TEST_ASSERT_TRUE(data.cycle_count_valid);
    TEST_ASSERT_EQUAL_UINT32(0x000300AA, data.cycle_count);

    TEST_ASSERT_EQUAL_UINT32(25, (uint32_t)data.register_count);
    TEST_ASSERT_EQUAL_UINT16(0x0020, data.registers[0].address);
}

static bool s_publish_called = false;
static event_bus_event_t s_published_event = {0};
static bool s_listener_called = false;
static uart_bms_live_data_t s_listener_data = {0};

static bool publish_stub(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;
    s_publish_called = true;
    s_published_event = *event;
    return true;
}

static void listener_stub(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    s_listener_called = true;
    if (data != NULL) {
        s_listener_data = *data;
    }
}

TEST_CASE("process frame dispatches listener and event", "[uart_bms]")
{
    uint16_t values[11] = {0};
    values[0] = 5000;
    values[1] = (uint16_t)(int16_t)200; // 20.0 A
    values[4] = 5000; // 50%

    uint8_t frame[3 + sizeof(values) + 2] = {0};
    size_t frame_len = 0;
    build_frame(values, 11, frame, &frame_len);

    s_publish_called = false;
    s_listener_called = false;
    memset(&s_published_event, 0, sizeof(s_published_event));
    memset(&s_listener_data, 0, sizeof(s_listener_data));

    uart_bms_set_event_publisher(publish_stub);
    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_register_listener(listener_stub, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, uart_bms_process_frame(frame, frame_len));

    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_TRUE(s_listener_called);
    TEST_ASSERT_EQUAL(UART_BMS_EVENT_ID_LIVE_DATA, s_published_event.id);
    TEST_ASSERT_EQUAL(sizeof(uart_bms_live_data_t), s_published_event.payload_size);
    TEST_ASSERT_NOT_NULL(s_published_event.payload);

    TEST_ASSERT_TRUE(s_listener_data.pack_voltage_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, s_listener_data.pack_voltage_v);

    uart_bms_unregister_listener(listener_stub, NULL);
    uart_bms_set_event_publisher(NULL);
}

TEST_CASE("process frame rejects invalid crc", "[uart_bms]")
{
    uint16_t values[2] = {1000, 2000};
    uint8_t frame[3 + sizeof(values) + 2] = {0};
    size_t frame_len = 0;
    build_frame(values, 2, frame, &frame_len);

    frame[frame_len - 2] ^= 0xFF; // Corrupt CRC

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, uart_bms_process_frame(frame, frame_len));
}

