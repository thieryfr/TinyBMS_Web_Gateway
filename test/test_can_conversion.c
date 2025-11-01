#include "unity.h"

#include "can_publisher.h"
#include "conversion_table.h"
#include "cvl_controller.h"

#include <math.h>
#include <string.h>

#ifdef __has_include
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"
#  endif
#endif

#ifndef CONFIG_TINYBMS_CAN_MANUFACTURER
#define CONFIG_TINYBMS_CAN_MANUFACTURER "TinyBMS"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_NAME
#define CONFIG_TINYBMS_CAN_BATTERY_NAME "Lithium Battery"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_FAMILY
#define CONFIG_TINYBMS_CAN_BATTERY_FAMILY CONFIG_TINYBMS_CAN_BATTERY_NAME
#endif

#define PGN_INVERTER_HANDSHAKE 0x307U
#define PGN_CVL_CCL_DCL      0x351U
#define PGN_SOC_SOH          0x355U
#define PGN_VOLTAGE_CURRENT  0x356U
#define PGN_ALARMS           0x35AU
#define PGN_MANUFACTURER     0x35EU
#define PGN_BATTERY_INFO     0x35FU
#define PGN_BMS_NAME_PART1   0x370U
#define PGN_BMS_NAME_PART2   0x371U
#define PGN_MODULE_STATUS    0x372U
#define PGN_CELL_EXTREMES    0x373U
#define PGN_MIN_CELL_ID      0x374U
#define PGN_MAX_CELL_ID      0x375U
#define PGN_MIN_TEMP_ID      0x376U
#define PGN_MAX_TEMP_ID      0x377U
#define PGN_ENERGY_COUNTERS  0x378U
#define PGN_INSTALLED_CAP    0x379U
#define PGN_SERIAL_PART1     0x380U
#define PGN_SERIAL_PART2     0x381U
#define PGN_BATTERY_FAMILY   0x382U

static const can_publisher_channel_t *find_channel(uint16_t pgn)
{
    for (size_t i = 0; i < g_can_publisher_channel_count; ++i) {
        if (g_can_publisher_channels[i].pgn == pgn) {
            return &g_can_publisher_channels[i];
        }
    }
    return NULL;
}

static void append_register(uart_bms_live_data_t *data, uint16_t address, uint16_t value)
{
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_TRUE_MESSAGE(data->register_count < UART_BMS_MAX_REGISTERS, "register overflow");
    data->registers[data->register_count].address = address;
    data->registers[data->register_count].raw_value = value;
    data->register_count++;
}

static void set_register(uart_bms_live_data_t *data, uint16_t address, uint16_t value)
{
    TEST_ASSERT_NOT_NULL(data);
    for (size_t i = 0; i < data->register_count; ++i) {
        if (data->registers[i].address == address) {
            data->registers[i].raw_value = value;
            return;
        }
    }
    append_register(data, address, value);
}

static void set_register_ascii(uart_bms_live_data_t *data, uint16_t base_address, const char *text)
{
    TEST_ASSERT_NOT_NULL(data);
    size_t length = strlen(text);
    size_t words = (length + 1U) / 2U;
    for (size_t i = 0; i < words; ++i) {
        uint8_t lo = 0U;
        uint8_t hi = 0U;
        size_t index = i * 2U;
        if (index < length) {
            lo = (uint8_t)text[index];
        }
        if ((index + 1U) < length) {
            hi = (uint8_t)text[index + 1U];
        }
        uint16_t value = (uint16_t)lo | ((uint16_t)hi << 8U);
        append_register(data, (uint16_t)(base_address + (uint16_t)i), value);
    }
}

static uart_bms_live_data_t make_nominal_sample(void)
{
    uart_bms_live_data_t data;
    memset(&data, 0, sizeof(data));
    data.timestamp_ms = 1000U;
    data.pack_voltage_v = 52.4f;
    data.pack_current_a = 18.5f;
    data.min_cell_mv = 3400U;
    data.max_cell_mv = 3485U;
    data.state_of_charge_pct = 78.3f;
    data.state_of_health_pct = 92.0f;
    data.average_temperature_c = 28.0f;
    data.auxiliary_temperature_c = 27.0f;
    data.mosfet_temperature_c = 31.0f;
    data.pack_temperature_min_c = 24.0f;
    data.pack_temperature_max_c = 34.0f;
    data.battery_capacity_ah = 280.0f;
    data.series_cell_count = 16U;
    data.overvoltage_cutoff_mv = 56700U;
    data.undervoltage_cutoff_mv = 44800U;
    data.discharge_overcurrent_limit_a = 140.0f;
    data.charge_overcurrent_limit_a = 120.0f;
    data.max_discharge_current_limit_a = 150.0f;
    data.max_charge_current_limit_a = 110.0f;
    data.peak_discharge_current_limit_a = 200.0f;
    data.overheat_cutoff_c = 75.0f;
    data.low_temp_charge_cutoff_c = -5.0f;
    data.register_count = 0;
    data.hardware_version = 0x12U;
    data.hardware_changes_version = 0x34U;
    data.firmware_version = 0x56U;
    data.firmware_flags = 0x78U;
    data.internal_firmware_version = 0x9ABCU;
    strncpy(data.serial_number, "SN1234567890ABCD", sizeof(data.serial_number) - 1U);
    data.serial_number[sizeof(data.serial_number) - 1U] = '\0';
    data.serial_length = (uint8_t)strlen(data.serial_number);
    set_register(&data, 0x0066U, 1500U);
    set_register(&data, 0x0067U, 1100U);
    append_register(&data, 0x0132U, 28000U);
    set_register_ascii(&data, 0x01F4U, "TinyBMS Maker");
    set_register_ascii(&data, 0x01F6U, "Nominal Pack 48V");
    set_register_ascii(&data, 0x01FAU, "SN1234567890ABCD");
    return data;
}

TEST_CASE("can_conversion_charge_limits_from_bms", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_CVL_CCL_DCL);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0237, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8))); // 56.7 V
    TEST_ASSERT_EQUAL_UINT16(0x044C, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8))); // 110.0 A
    TEST_ASSERT_EQUAL_UINT16(0x05DC, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8))); // 150.0 A
}

TEST_CASE("can_conversion_inverter_identifier", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_INVERTER_HANDSHAKE);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT8(data.hardware_version, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(data.hardware_changes_version, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(data.firmware_version, frame.data[2]);
    TEST_ASSERT_EQUAL_UINT8(data.firmware_flags, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8('V', frame.data[4]);
    TEST_ASSERT_EQUAL_UINT8('I', frame.data[5]);
    TEST_ASSERT_EQUAL_UINT8('C', frame.data[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame.data[7]);
}

TEST_CASE("can_conversion_charge_limits_from_cvl", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    data.pack_current_a = -25.0f;
    data.timestamp_ms = 2000U;

    can_publisher_cvl_init();
    can_publisher_cvl_prepare(&data);

    can_publisher_cvl_result_t cvl_result;
    TEST_ASSERT_TRUE(can_publisher_cvl_get_latest(&cvl_result));

    const can_publisher_channel_t *channel = find_channel(PGN_CVL_CCL_DCL);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));

    uint16_t cvl_raw = (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8));
    uint16_t ccl_raw = (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8));
    uint16_t dcl_raw = (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8));

    TEST_ASSERT_EQUAL_UINT16((uint16_t)lrintf(cvl_result.result.cvl_voltage_v * 10.0f), cvl_raw);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)lrintf(cvl_result.result.ccl_limit_a * 10.0f), ccl_raw);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)lrintf(cvl_result.result.dcl_limit_a * 10.0f), dcl_raw);
}

TEST_CASE("can_conversion_soc_soh_range_handling", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_SOC_SOH);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(78, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(92, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));

    data.state_of_charge_pct = 135.0f;
    data.state_of_health_pct = -12.0f;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(100, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));
}

TEST_CASE("can_conversion_battery_name_part1_ascii", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_BMS_NAME_PART1);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("Nominal ", frame.data, 8);
}

TEST_CASE("can_conversion_module_status_counts", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_MODULE_STATUS);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(1U, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(frame.data[6] | ((uint16_t)frame.data[7] << 8)));

    data.max_charge_current_limit_a = 0.0f;
    data.max_discharge_current_limit_a = 0.0f;
    data.warning_bits = 1U;
    data.alarm_bits = 1U;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(1U, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(1U, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(1U, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));

    data.timestamp_ms = 0U;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(1U, (uint16_t)(frame.data[6] | ((uint16_t)frame.data[7] << 8)));
}

TEST_CASE("can_conversion_cell_extremes_pgn", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_CELL_EXTREMES);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(data.min_cell_mv, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(data.max_cell_mv, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(297U, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));
    TEST_ASSERT_EQUAL_UINT16(307U, (uint16_t)(frame.data[6] | ((uint16_t)frame.data[7] << 8)));
}

TEST_CASE("can_conversion_cell_identifier_strings", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *min_cell = find_channel(PGN_MIN_CELL_ID);
    const can_publisher_channel_t *max_cell = find_channel(PGN_MAX_CELL_ID);
    const can_publisher_channel_t *min_temp = find_channel(PGN_MIN_TEMP_ID);
    const can_publisher_channel_t *max_temp = find_channel(PGN_MAX_TEMP_ID);
    TEST_ASSERT_NOT_NULL(min_cell);
    TEST_ASSERT_NOT_NULL(max_cell);
    TEST_ASSERT_NOT_NULL(min_temp);
    TEST_ASSERT_NOT_NULL(max_temp);

    can_publisher_frame_t frame = { .id = min_cell->can_id, .dlc = min_cell->dlc };
    TEST_ASSERT_TRUE(min_cell->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("MINV3400", frame.data, 8);

    frame.id = max_cell->can_id;
    frame.dlc = max_cell->dlc;
    TEST_ASSERT_TRUE(max_cell->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("MAXV3485", frame.data, 8);

    frame.id = min_temp->can_id;
    frame.dlc = min_temp->dlc;
    TEST_ASSERT_TRUE(min_temp->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("MINT+024", frame.data, 8);

    frame.id = max_temp->can_id;
    frame.dlc = max_temp->dlc;
    TEST_ASSERT_TRUE(max_temp->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("MAXT+034", frame.data, 8);
}

TEST_CASE("can_conversion_serial_number_frames", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *part1 = find_channel(PGN_SERIAL_PART1);
    const can_publisher_channel_t *part2 = find_channel(PGN_SERIAL_PART2);
    TEST_ASSERT_NOT_NULL(part1);
    TEST_ASSERT_NOT_NULL(part2);

    can_publisher_frame_t frame = { .id = part1->can_id, .dlc = part1->dlc };
    TEST_ASSERT_TRUE(part1->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("SN123456", frame.data, 8);

    frame.id = part2->can_id;
    frame.dlc = part2->dlc;
    TEST_ASSERT_TRUE(part2->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("7890ABCD", frame.data, 8);
}

TEST_CASE("can_conversion_voltage_current_temperature_extremes", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_VOLTAGE_CURRENT);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(5240, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_INT16(185, (int16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_INT16(310, (int16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));

    data.pack_voltage_v = 800.0f;
    data.pack_current_a = -500.0f;
    data.mosfet_temperature_c = -120.0f;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_INT16(-32768, (int16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_INT16(-1200, (int16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));
}

TEST_CASE("can_conversion_soc_soh_high_resolution_field", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_SOC_SOH);
    TEST_ASSERT_NOT_NULL(channel);

    uint32_t soc_register_value = 50345678U; // 50.345678 %
    data.state_of_charge_pct = (float)soc_register_value * 0.000001f;
    set_register(&data, 0x002E, (uint16_t)(soc_register_value & 0xFFFFU));
    set_register(&data, 0x002F, (uint16_t)((soc_register_value >> 16U) & 0xFFFFU));

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(50, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(92, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(5035, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));

    soc_register_value = 150432198U; // > 100 %, expect saturation
    data.state_of_charge_pct = 150.432198f;
    set_register(&data, 0x002E, (uint16_t)(soc_register_value & 0xFFFFU));
    set_register(&data, 0x002F, (uint16_t)((soc_register_value >> 16U) & 0xFFFFU));

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(100, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8)));
    TEST_ASSERT_EQUAL_UINT16(92, (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8)));
    TEST_ASSERT_EQUAL_UINT16(10000, (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8)));
}

TEST_CASE("can_conversion_alarm_levels", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    data.pack_voltage_v = 40.0f;
    data.undervoltage_cutoff_mv = 42000U;
    data.overvoltage_cutoff_mv = 50000U;
    data.mosfet_temperature_c = 85.0f;
    data.pack_temperature_max_c = 90.0f;
    data.pack_temperature_min_c = -15.0f;
    data.max_cell_mv = 3500U;
    data.min_cell_mv = 3400U;
    data.state_of_charge_pct = 4.0f;
    data.pack_current_a = 2.0f;

    const can_publisher_channel_t *channel = find_channel(PGN_ALARMS);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT8(0xA2, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x32, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8(0xA2, frame.data[4]);
    TEST_ASSERT_EQUAL_UINT8(0x02, frame.data[5]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[6]);
    TEST_ASSERT_EQUAL_UINT8(0xF2, frame.data[7]);
}

TEST_CASE("can_conversion_alarm_status_warning_levels", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    float overvoltage_v = (float)data.overvoltage_cutoff_mv / 1000.0f;
    data.pack_voltage_v = overvoltage_v * 0.97f;
    data.pack_temperature_max_c = data.overheat_cutoff_c * 0.92f;
    data.mosfet_temperature_c = data.pack_temperature_max_c;
    data.pack_temperature_min_c = -4.0f;
    data.auxiliary_temperature_c = data.low_temp_charge_cutoff_c + 1.0f;
    data.max_cell_mv = 3450U;
    data.min_cell_mv = 3390U;
    data.pack_current_a = -data.discharge_overcurrent_limit_a * 0.85f;

    const can_publisher_channel_t *channel = find_channel(PGN_ALARMS);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT8(0x00, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x30, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8(0x45, frame.data[4]);
    TEST_ASSERT_EQUAL_UINT8(0x51, frame.data[5]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[6]);
    TEST_ASSERT_EQUAL_UINT8(0xF1, frame.data[7]);
}

TEST_CASE("can_conversion_alarm_status_charge_current_levels", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    data.pack_current_a = data.charge_overcurrent_limit_a * 1.05f;
    data.max_cell_mv = 3400U;
    data.min_cell_mv = 3400U;

    const can_publisher_channel_t *channel = find_channel(PGN_ALARMS);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT8(0x02, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x30, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, frame.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8(0x02, frame.data[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame.data[5]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, frame.data[6]);
    TEST_ASSERT_EQUAL_UINT8(0xF0, frame.data[7]);
}

TEST_CASE("can_conversion_manufacturer_and_battery_strings", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *manufacturer = find_channel(PGN_MANUFACTURER);
    const can_publisher_channel_t *battery = find_channel(PGN_BATTERY_INFO);
    const can_publisher_channel_t *part2 = find_channel(PGN_BMS_NAME_PART2);
    const can_publisher_channel_t *family = find_channel(PGN_BATTERY_FAMILY);
    TEST_ASSERT_NOT_NULL(manufacturer);
    TEST_ASSERT_NOT_NULL(battery);
    TEST_ASSERT_NOT_NULL(part2);
    TEST_ASSERT_NOT_NULL(family);

    can_publisher_frame_t frame = { .id = manufacturer->can_id, .dlc = manufacturer->dlc };
    TEST_ASSERT_TRUE(manufacturer->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("TinyBMS ", frame.data, 8);

    frame.id = battery->can_id;
    frame.dlc = battery->dlc;
    TEST_ASSERT_TRUE(battery->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(0x3412U,
                             (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8))); // HW/changes
    TEST_ASSERT_EQUAL_UINT16(0x7856U,
                             (uint16_t)(frame.data[2] | ((uint16_t)frame.data[3] << 8))); // Public FW/flags
    TEST_ASSERT_EQUAL_UINT16(28000U,
                             (uint16_t)(frame.data[4] | ((uint16_t)frame.data[5] << 8))); // Capacity ×100
    TEST_ASSERT_EQUAL_UINT16(0x9ABCU,
                             (uint16_t)(frame.data[6] | ((uint16_t)frame.data[7] << 8))); // Internal FW

    frame.id = part2->can_id;
    frame.dlc = part2->dlc;
    TEST_ASSERT_TRUE(part2->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("Pack 48V", frame.data, 8);

    frame.id = family->can_id;
    frame.dlc = family->dlc;
    set_register_ascii(&data, 0x01F8U, "Family16");
    TEST_ASSERT_TRUE(family->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_MEMORY("Family16", frame.data, 8);
    data.register_count = 0;
    TEST_ASSERT_TRUE(family->fill_fn(&data, &frame));
    size_t family_length = strlen(CONFIG_TINYBMS_CAN_BATTERY_FAMILY);
    size_t compare = (family_length < frame.dlc) ? family_length : frame.dlc;
    TEST_ASSERT_EQUAL_MEMORY(CONFIG_TINYBMS_CAN_BATTERY_FAMILY, frame.data, compare);
}

TEST_CASE("can_conversion_energy_counters_accumulate", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_ENERGY_COUNTERS);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_conversion_reset_state();

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0U, frame.data[i]);
    }

    data.timestamp_ms += 600000U; // +10 minutes
    data.pack_current_a = 20.0f;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT32(1U, (uint32_t)(frame.data[0] | ((uint32_t)frame.data[1] << 8) |
                                           ((uint32_t)frame.data[2] << 16) |
                                           ((uint32_t)frame.data[3] << 24)));
    TEST_ASSERT_EQUAL_UINT32(0U, (uint32_t)(frame.data[4] | ((uint32_t)frame.data[5] << 8) |
                                           ((uint32_t)frame.data[6] << 16) |
                                           ((uint32_t)frame.data[7] << 24)));

    data.timestamp_ms += 600000U;
    data.pack_current_a = -30.0f;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT32(1U, (uint32_t)(frame.data[0] | ((uint32_t)frame.data[1] << 8) |
                                           ((uint32_t)frame.data[2] << 16) |
                                           ((uint32_t)frame.data[3] << 24)));
    TEST_ASSERT_EQUAL_UINT32(2U, (uint32_t)(frame.data[4] | ((uint32_t)frame.data[5] << 8) |
                                           ((uint32_t)frame.data[6] << 16) |
                                           ((uint32_t)frame.data[7] << 24)));
}

TEST_CASE("can_conversion_installed_capacity_sources", "[can][unit]")
{
    uart_bms_live_data_t data = make_nominal_sample();
    const can_publisher_channel_t *channel = find_channel(PGN_INSTALLED_CAP);
    TEST_ASSERT_NOT_NULL(channel);

    can_publisher_frame_t frame = {
        .id = channel->can_id,
        .dlc = channel->dlc,
    };

    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(258, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8))); // 280 Ah * 0.92

    data.battery_capacity_ah = 0.0f;
    data.state_of_health_pct = 50.0f;
    TEST_ASSERT_TRUE(channel->fill_fn(&data, &frame));
    TEST_ASSERT_EQUAL_UINT16(20, (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8))); // 16 * 2.5 * 0.5
}
