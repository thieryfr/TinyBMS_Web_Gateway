#include "unity.h"

#include <string.h>

#include "can_publisher.h"
#include "conversion_table.h"
#include "uart_bms.h"
#include "esp_err.h"

#define PGN_ENERGY_COUNTERS 0x378U

static const can_publisher_channel_t *find_channel(uint16_t pgn)
{
    for (size_t i = 0; i < g_can_publisher_channel_count; ++i) {
        if (g_can_publisher_channels[i].pgn == pgn) {
            return &g_can_publisher_channels[i];
        }
    }
    return NULL;
}

TEST_CASE("energy_counters_restored_after_restart", "[persistence][energy]")
{
    can_publisher_conversion_reset_state();

    const double charged_wh = 43210.5;
    const double discharged_wh = 12345.5;

    can_publisher_conversion_set_energy_state(charged_wh, discharged_wh);
    TEST_ASSERT_EQUAL(ESP_OK, can_publisher_conversion_persist_energy_state());

    can_publisher_conversion_set_energy_state(0.0, 0.0);

    esp_err_t restore_err = can_publisher_conversion_restore_energy_state();
    TEST_ASSERT_EQUAL(ESP_OK, restore_err);

    double restored_in = 0.0;
    double restored_out = 0.0;
    can_publisher_conversion_get_energy_state(&restored_in, &restored_out);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, charged_wh, restored_in);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, discharged_wh, restored_out);

    const can_publisher_channel_t *channel = find_channel(PGN_ENERGY_COUNTERS);
    TEST_ASSERT_NOT_NULL(channel);

    uart_bms_live_data_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.timestamp_ms = 1000U;
    sample.pack_voltage_v = 52.0f;
    sample.pack_current_a = 0.0f;

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&sample, &frame));

    uint32_t encoded_in = (uint32_t)frame.data[0] |
                          ((uint32_t)frame.data[1] << 8U) |
                          ((uint32_t)frame.data[2] << 16U) |
                          ((uint32_t)frame.data[3] << 24U);
    uint32_t encoded_out = (uint32_t)frame.data[4] |
                           ((uint32_t)frame.data[5] << 8U) |
                           ((uint32_t)frame.data[6] << 16U) |
                           ((uint32_t)frame.data[7] << 24U);

    uint32_t expected_in = (uint32_t)((charged_wh / 100.0) + 0.5);
    uint32_t expected_out = (uint32_t)((discharged_wh / 100.0) + 0.5);

    TEST_ASSERT_EQUAL_UINT32(expected_in, encoded_in);
    TEST_ASSERT_EQUAL_UINT32(expected_out, encoded_out);
}

