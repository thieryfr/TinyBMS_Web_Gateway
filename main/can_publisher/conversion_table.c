#include "conversion_table.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static uint16_t clamp_u16(int32_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 0xFFFF) {
        return 0xFFFF;
    }
    return (uint16_t)value;
}

static int16_t clamp_i16(int32_t value)
{
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
}

static uint8_t clamp_u8(int32_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 0xFF) {
        return 0xFF;
    }
    return (uint8_t)value;
}

static uint8_t encode_temperature(float value_c)
{
    int32_t scaled = (int32_t)lrintf(value_c);
    if (scaled < -128) {
        scaled = -128;
    }
    if (scaled > 127) {
        scaled = 127;
    }
    return (uint8_t)((int8_t)scaled);
}

static bool encode_pack_status(const uart_bms_live_data_t *data, can_publisher_frame_t *out_frame)
{
    if (data == NULL || out_frame == NULL) {
        return false;
    }

    memset(out_frame->data, 0, sizeof(out_frame->data));

    uint16_t voltage_raw = clamp_u16((int32_t)lrintf(data->pack_voltage_v * 100.0f));
    int16_t current_raw = clamp_i16((int32_t)lrintf(data->pack_current_a * 10.0f));
    uint8_t soc_raw = clamp_u8((int32_t)lrintf(data->state_of_charge_pct));
    uint8_t soh_raw = clamp_u8((int32_t)lrintf(data->state_of_health_pct));

    out_frame->data[0] = (uint8_t)(voltage_raw & 0xFFU);
    out_frame->data[1] = (uint8_t)((voltage_raw >> 8U) & 0xFFU);
    out_frame->data[2] = (uint8_t)(current_raw & 0xFF);
    out_frame->data[3] = (uint8_t)((current_raw >> 8) & 0xFF);
    out_frame->data[4] = soc_raw;
    out_frame->data[5] = soh_raw;
    out_frame->data[6] = encode_temperature(data->average_temperature_c);
    out_frame->data[7] = encode_temperature(data->mosfet_temperature_c);

    return true;
}

static bool encode_alarm_summary(const uart_bms_live_data_t *data, can_publisher_frame_t *out_frame)
{
    if (data == NULL || out_frame == NULL) {
        return false;
    }

    memset(out_frame->data, 0, sizeof(out_frame->data));

    out_frame->data[0] = (uint8_t)(data->alarm_bits & 0xFFU);
    out_frame->data[1] = (uint8_t)((data->alarm_bits >> 8U) & 0xFFU);
    out_frame->data[2] = (uint8_t)(data->warning_bits & 0xFFU);
    out_frame->data[3] = (uint8_t)((data->warning_bits >> 8U) & 0xFFU);
    out_frame->data[4] = (uint8_t)(data->balancing_bits & 0xFFU);
    out_frame->data[5] = (uint8_t)((data->balancing_bits >> 8U) & 0xFFU);

    uint16_t uptime_minutes = (uint16_t)((data->uptime_seconds / 60U) & 0xFFFFU);
    out_frame->data[6] = (uint8_t)(uptime_minutes & 0xFFU);
    out_frame->data[7] = (uint8_t)((uptime_minutes >> 8U) & 0xFFU);

    return true;
}

static bool encode_cell_statistics(const uart_bms_live_data_t *data, can_publisher_frame_t *out_frame)
{
    if (data == NULL || out_frame == NULL) {
        return false;
    }

    memset(out_frame->data, 0, sizeof(out_frame->data));

    out_frame->data[0] = (uint8_t)(data->min_cell_mv & 0xFFU);
    out_frame->data[1] = (uint8_t)((data->min_cell_mv >> 8U) & 0xFFU);
    out_frame->data[2] = (uint8_t)(data->max_cell_mv & 0xFFU);
    out_frame->data[3] = (uint8_t)((data->max_cell_mv >> 8U) & 0xFFU);

    uint16_t cycle_count = (uint16_t)(data->cycle_count & 0xFFFFU);
    out_frame->data[4] = (uint8_t)(cycle_count & 0xFFU);
    out_frame->data[5] = (uint8_t)((cycle_count >> 8U) & 0xFFU);

    uint16_t capacity_dah = clamp_u16((int32_t)lrintf(data->battery_capacity_ah * 10.0f));
    out_frame->data[6] = (uint8_t)(capacity_dah & 0xFFU);
    out_frame->data[7] = (uint8_t)((capacity_dah >> 8U) & 0xFFU);

    return true;
}

const can_publisher_channel_t g_can_publisher_channels[] = {
    {
        .can_id = 0x18FF50E5,
        .dlc = 8,
        .fill_fn = encode_pack_status,
        .description = "TinyBMS pack status",
    },
    {
        .can_id = 0x18FF01E5,
        .dlc = 8,
        .fill_fn = encode_alarm_summary,
        .description = "TinyBMS alarm summary",
    },
    {
        .can_id = 0x18FF02E5,
        .dlc = 8,
        .fill_fn = encode_cell_statistics,
        .description = "TinyBMS cell statistics",
    },
};

const size_t g_can_publisher_channel_count = sizeof(g_can_publisher_channels) / sizeof(g_can_publisher_channels[0]);

