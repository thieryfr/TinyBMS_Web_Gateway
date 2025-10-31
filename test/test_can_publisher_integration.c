#include "unity.h"

#include "can_publisher.h"
#include "conversion_table.h"
#include "cvl_controller.h"

#include <string.h>

static uart_bms_live_data_t make_sample(void)
{
    uart_bms_live_data_t data;
    memset(&data, 0, sizeof(data));
    data.timestamp_ms = 4000U;
    data.pack_voltage_v = 53.1f;
    data.pack_current_a = -12.5f;
    data.min_cell_mv = 3405U;
    data.max_cell_mv = 3495U;
    data.state_of_charge_pct = 71.2f;
    data.state_of_health_pct = 88.0f;
    data.mosfet_temperature_c = 33.0f;
    data.pack_temperature_min_c = 23.0f;
    data.pack_temperature_max_c = 36.0f;
    data.battery_capacity_ah = 300.0f;
    data.series_cell_count = 16U;
    data.overvoltage_cutoff_mv = 56600U;
    data.undervoltage_cutoff_mv = 45200U;
    data.discharge_overcurrent_limit_a = 160.0f;
    data.charge_overcurrent_limit_a = 110.0f;
    data.peak_discharge_current_limit_a = 180.0f;
    data.overheat_cutoff_c = 70.0f;
    data.register_count = 0;

    const char manufacturer[] = "Integration Labs";
    const char battery_name[] = "Integration Pack";

    size_t manufacturer_words = (sizeof(manufacturer) - 1U + 1U) / 2U;
    for (size_t i = 0; i < manufacturer_words; ++i) {
        uint8_t lo = (i * 2U < (sizeof(manufacturer) - 1U)) ? (uint8_t)manufacturer[i * 2U] : 0U;
        uint8_t hi = (i * 2U + 1U < (sizeof(manufacturer) - 1U)) ? (uint8_t)manufacturer[i * 2U + 1U] : 0U;
        data.registers[data.register_count].address = (uint16_t)(0x01F4U + (uint16_t)i);
        data.registers[data.register_count].raw_value = (uint16_t)lo | ((uint16_t)hi << 8U);
        data.register_count++;
    }

    size_t name_words = (sizeof(battery_name) - 1U + 1U) / 2U;
    for (size_t i = 0; i < name_words; ++i) {
        uint8_t lo = (i * 2U < (sizeof(battery_name) - 1U)) ? (uint8_t)battery_name[i * 2U] : 0U;
        uint8_t hi = (i * 2U + 1U < (sizeof(battery_name) - 1U)) ? (uint8_t)battery_name[i * 2U + 1U] : 0U;
        data.registers[data.register_count].address = (uint16_t)(0x01F6U + (uint16_t)i);
        data.registers[data.register_count].raw_value = (uint16_t)lo | ((uint16_t)hi << 8U);
        data.register_count++;
    }

    return data;
}

TEST_CASE("can_publisher_populates_buffer_for_all_channels", "[can][integration]")
{
    uart_bms_live_data_t sample = make_sample();

    can_publisher_frame_t expected[CAN_PUBLISHER_MAX_BUFFER_SLOTS];
    memset(expected, 0, sizeof(expected));

    can_publisher_conversion_reset_state();
    can_publisher_cvl_init();
    can_publisher_cvl_prepare(&sample);

    size_t channels = g_can_publisher_channel_count;
    TEST_ASSERT(channels <= CAN_PUBLISHER_MAX_BUFFER_SLOTS);

    for (size_t i = 0; i < channels; ++i) {
        const can_publisher_channel_t *channel = &g_can_publisher_channels[i];
        expected[i].id = channel->can_id;
        expected[i].dlc = channel->dlc;
        TEST_ASSERT_TRUE(channel->fill_fn(&sample, &expected[i]));
    }

    can_publisher_conversion_reset_state();
    can_publisher_cvl_init();

    can_publisher_buffer_t buffer = { .slots = {0}, .slot_valid = {0}, .capacity = channels };
    can_publisher_registry_t registry = {
        .channels = g_can_publisher_channels,
        .channel_count = channels,
        .buffer = &buffer,
    };

    can_publisher_on_bms_update(&sample, &registry);

    for (size_t i = 0; i < channels; ++i) {
        const can_publisher_channel_t *channel = &g_can_publisher_channels[i];
        TEST_ASSERT_TRUE_MESSAGE(buffer.slot_valid[i], "Frame missing for channel");
        TEST_ASSERT_EQUAL_UINT32(channel->can_id, buffer.slots[i].id);
        TEST_ASSERT_EQUAL_UINT8(channel->dlc, buffer.slots[i].dlc);
        TEST_ASSERT_EQUAL_UINT64(sample.timestamp_ms, buffer.slots[i].timestamp_ms);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i].data, buffer.slots[i].data, channel->dlc);
    }
}
