#include "conversion_table.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "cvl_controller.h"

#define VICTRON_PGN_CVL_CCL_DCL      0x351U
#define VICTRON_PGN_SOC_SOH          0x355U
#define VICTRON_PGN_VOLTAGE_CURRENT  0x356U
#define VICTRON_PGN_ALARMS           0x35AU
#define VICTRON_PGN_MANUFACTURER     0x35EU
#define VICTRON_PGN_BATTERY_INFO     0x35FU
#define VICTRON_PGN_BMS_NAME_PART2   0x371U
#define VICTRON_PGN_ENERGY_COUNTERS  0x378U
#define VICTRON_PGN_INSTALLED_CAP    0x379U
#define VICTRON_PGN_BATTERY_FAMILY   0x382U

#define VICTRON_PRIORITY             6U
#define VICTRON_SOURCE_ADDRESS       0xE5U
#define VICTRON_EXTENDED_ID(pgn) \
    ((((uint32_t)VICTRON_PRIORITY) << 26) | ((uint32_t)(pgn) << 8) | (uint32_t)VICTRON_SOURCE_ADDRESS)

#ifndef CONFIG_TINYBMS_CAN_MANUFACTURER
#define CONFIG_TINYBMS_CAN_MANUFACTURER "TinyBMS"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_NAME
#define CONFIG_TINYBMS_CAN_BATTERY_NAME "Lithium Battery"
#endif

#ifndef CONFIG_TINYBMS_CAN_BATTERY_FAMILY
#define CONFIG_TINYBMS_CAN_BATTERY_FAMILY CONFIG_TINYBMS_CAN_BATTERY_NAME
#endif

static const char *TAG = "can_conv";

static double s_energy_charged_wh = 0.0;
static double s_energy_discharged_wh = 0.0;
static uint64_t s_energy_last_timestamp_ms = 0;

void can_publisher_conversion_reset_state(void)
{
    s_energy_charged_wh = 0.0;
    s_energy_discharged_wh = 0.0;
    s_energy_last_timestamp_ms = 0;
}

static inline uint16_t clamp_u16(int32_t value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 0xFFFF) {
        return 0xFFFFU;
    }
    return (uint16_t)value;
}

static inline int16_t clamp_i16(int32_t value)
{
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
}

static inline uint8_t sanitize_ascii(uint8_t value)
{
    value &= 0x7FU;
    if (value < 0x20U && value != 0U) {
        value = 0x20U;
    }
    return value;
}

static uint16_t encode_u16_scaled(float value, float scale, float offset, uint16_t min_value, uint16_t max_value)
{
    double scaled = ((double)value + (double)offset) * (double)scale;
    if (!isfinite(scaled)) {
        return min_value;
    }
    long rounded = lrint(scaled);
    if (rounded < (long)min_value) {
        return min_value;
    }
    if (rounded > (long)max_value) {
        return max_value;
    }
    return (uint16_t)rounded;
}

static int16_t encode_i16_scaled(float value, float scale)
{
    double scaled = (double)value * (double)scale;
    if (!isfinite(scaled)) {
        return 0;
    }
    long rounded = lrint(scaled);
    return clamp_i16((int32_t)rounded);
}

static uint8_t encode_2bit_field(uint8_t current, size_t index, uint8_t level)
{
    size_t shift = (index & 0x3U) * 2U;
    current &= (uint8_t)~(0x3U << shift);
    current |= (uint8_t)((level & 0x3U) << shift);
    return current;
}

static bool find_register_value(const uart_bms_live_data_t *data, uint16_t address, uint16_t *out_value)
{
    if (data == NULL || out_value == NULL) {
        return false;
    }

    for (size_t i = 0; i < data->register_count; ++i) {
        if (data->registers[i].address == address) {
            *out_value = data->registers[i].raw_value;
            return true;
        }
    }
    return false;
}

static size_t read_register_block(const uart_bms_live_data_t *data,
                                  uint16_t base_address,
                                  size_t word_count,
                                  uint16_t *out_words)
{
    if (data == NULL || out_words == NULL) {
        return 0;
    }

    size_t found = 0;
    for (size_t i = 0; i < word_count; ++i) {
        uint16_t address = (uint16_t)(base_address + (uint16_t)i);
        if (find_register_value(data, address, &out_words[i])) {
            ++found;
        } else {
            out_words[i] = 0U;
        }
    }
    return found;
}

static bool decode_ascii_from_registers(const uart_bms_live_data_t *data,
                                        uint16_t base_address,
                                        size_t char_count,
                                        char *out_buffer,
                                        size_t buffer_size)
{
    if (out_buffer == NULL || buffer_size == 0) {
        return false;
    }

    memset(out_buffer, 0, buffer_size);

    if (data == NULL) {
        return false;
    }

    size_t word_count = (char_count + 1U) / 2U;
    uint16_t words[8] = {0};
    if (word_count > (sizeof(words) / sizeof(words[0]))) {
        word_count = sizeof(words) / sizeof(words[0]);
    }

    size_t available = read_register_block(data, base_address, word_count, words);
    if (available == 0) {
        return false;
    }

    for (size_t i = 0; i < char_count && i < (buffer_size - 1U); ++i) {
        size_t word_index = i / 2U;
        bool high_byte = (i % 2U) != 0U;
        uint16_t raw = words[word_index];
        uint8_t c = high_byte ? (uint8_t)((raw >> 8U) & 0xFFU) : (uint8_t)(raw & 0xFFU);
        out_buffer[i] = (char)sanitize_ascii(c);
    }

    bool has_non_zero = false;
    for (size_t i = 0; i < char_count && i < (buffer_size - 1U); ++i) {
        if (out_buffer[i] != '\0' && out_buffer[i] != ' ') {
            has_non_zero = true;
            break;
        }
    }

    if (!has_non_zero) {
        memset(out_buffer, 0, buffer_size);
        return false;
    }

    return true;
}

static void copy_ascii_padded(uint8_t *dest, size_t length, const char *source, size_t offset)
{
    if (dest == NULL || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        uint8_t value = 0U;
        size_t index = offset + i;
        if (source != NULL && source[index] != '\0') {
            value = sanitize_ascii((uint8_t)source[index]);
        }
        dest[i] = value;
    }
}

static uint32_t encode_energy_wh(double energy_wh)
{
    if (!(energy_wh > 0.0)) {
        return 0U;
    }

    double scaled = energy_wh / 100.0;
    if (!isfinite(scaled) || scaled < 0.0) {
        return 0U;
    }
    if (scaled > 4294967295.0) {
        scaled = 4294967295.0;
    }
    return (uint32_t)(scaled + 0.5);
}

static void update_energy_counters(const uart_bms_live_data_t *data)
{
    if (data == NULL) {
        return;
    }

    if (data->timestamp_ms == 0U) {
        return;
    }

    if (s_energy_last_timestamp_ms == 0U) {
        s_energy_last_timestamp_ms = data->timestamp_ms;
        return;
    }

    uint64_t current_ts = data->timestamp_ms;
    if (current_ts <= s_energy_last_timestamp_ms) {
        s_energy_last_timestamp_ms = current_ts;
        return;
    }

    uint64_t delta_ms = current_ts - s_energy_last_timestamp_ms;
    s_energy_last_timestamp_ms = current_ts;

    if (delta_ms > 60000U) {
        ESP_LOGW(TAG, "Energy integration gap %" PRIu64 " ms", delta_ms);
    }

    double voltage = (double)data->pack_voltage_v;
    double current = (double)data->pack_current_a;
    if (!isfinite(voltage) || !isfinite(current) || voltage <= 0.1) {
        return;
    }

    double hours = (double)delta_ms / 3600000.0;
    double power_w = voltage * current;
    if (power_w >= 0.0) {
        s_energy_charged_wh += power_w * hours;
    } else {
        s_energy_discharged_wh += (-power_w) * hours;
    }

    if (s_energy_charged_wh < 0.0) {
        s_energy_charged_wh = 0.0;
    }
    if (s_energy_discharged_wh < 0.0) {
        s_energy_discharged_wh = 0.0;
    }
}

static const char *resolve_manufacturer_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, 0x01F4U, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    return CONFIG_TINYBMS_CAN_MANUFACTURER;
}

static const char *resolve_battery_name_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, 0x01F6U, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    return CONFIG_TINYBMS_CAN_BATTERY_NAME;
}

static const char *resolve_battery_family_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, 0x01F6U, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    return CONFIG_TINYBMS_CAN_BATTERY_FAMILY;
}

static float sanitize_positive(float value)
{
    if (!isfinite(value) || value < 0.0f) {
        return 0.0f;
    }
    return value;
}

static bool encode_charge_limits(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    float cvl_v = 0.0f;
    float ccl_a = 0.0f;
    float dcl_a = 0.0f;

    can_publisher_cvl_result_t cvl_result;
    bool have_cvl = can_publisher_cvl_get_latest(&cvl_result);
    if (have_cvl) {
        cvl_v = sanitize_positive(cvl_result.result.cvl_voltage_v);
        ccl_a = sanitize_positive(cvl_result.result.ccl_limit_a);
        dcl_a = sanitize_positive(cvl_result.result.dcl_limit_a);

        if (cvl_v <= 0.0f) {
            have_cvl = false;
        }
    }

    if (!have_cvl) {
        cvl_v = sanitize_positive(data->pack_voltage_v);
        if (data->overvoltage_cutoff_mv > 0U) {
            cvl_v = (float)data->overvoltage_cutoff_mv / 1000.0f;
        }

        ccl_a = sanitize_positive(data->charge_overcurrent_limit_a);
        if (ccl_a <= 0.0f && data->peak_discharge_current_limit_a > 0.0f) {
            ccl_a = sanitize_positive(data->peak_discharge_current_limit_a);
        }

        dcl_a = sanitize_positive(data->discharge_overcurrent_limit_a);
        if (dcl_a <= 0.0f && data->peak_discharge_current_limit_a > 0.0f) {
            dcl_a = sanitize_positive(data->peak_discharge_current_limit_a);
        }
    }

    uint16_t cvl_raw = encode_u16_scaled(cvl_v, 100.0f, 0.0f, 0U, 0xFFFFU);
    uint16_t ccl_raw = encode_u16_scaled(ccl_a, 10.0f, 0.0f, 0U, 0xFFFFU);
    uint16_t dcl_raw = encode_u16_scaled(dcl_a, 10.0f, 0.0f, 0U, 0xFFFFU);

    frame->data[0] = (uint8_t)(cvl_raw & 0xFFU);
    frame->data[1] = (uint8_t)((cvl_raw >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(ccl_raw & 0xFFU);
    frame->data[3] = (uint8_t)((ccl_raw >> 8U) & 0xFFU);
    frame->data[4] = (uint8_t)(dcl_raw & 0xFFU);
    frame->data[5] = (uint8_t)((dcl_raw >> 8U) & 0xFFU);

    return true;
}

static bool encode_soc_soh(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    uint16_t soc_raw = encode_u16_scaled(data->state_of_charge_pct, 10.0f, 0.0f, 0U, 1000U);
    uint16_t soh_raw = encode_u16_scaled(data->state_of_health_pct, 10.0f, 0.0f, 0U, 1000U);

    frame->data[0] = (uint8_t)(soc_raw & 0xFFU);
    frame->data[1] = (uint8_t)((soc_raw >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(soh_raw & 0xFFU);
    frame->data[3] = (uint8_t)((soh_raw >> 8U) & 0xFFU);

    return true;
}

static bool encode_voltage_current_temperature(const uart_bms_live_data_t *data,
                                               can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    uint16_t voltage_raw = encode_u16_scaled(data->pack_voltage_v, 100.0f, 0.0f, 0U, 0xFFFFU);
    int16_t current_raw = encode_i16_scaled(data->pack_current_a, 10.0f);
    int16_t temperature_raw = encode_i16_scaled(data->mosfet_temperature_c, 10.0f);

    frame->data[0] = (uint8_t)(voltage_raw & 0xFFU);
    frame->data[1] = (uint8_t)((voltage_raw >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(current_raw & 0xFFU);
    frame->data[3] = (uint8_t)((current_raw >> 8U) & 0xFFU);
    frame->data[4] = (uint8_t)(temperature_raw & 0xFFU);
    frame->data[5] = (uint8_t)((temperature_raw >> 8U) & 0xFFU);

    return true;
}

static bool encode_alarm_status(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    float pack_voltage_v = data->pack_voltage_v;
    float undervoltage_v = (data->undervoltage_cutoff_mv > 0U) ? ((float)data->undervoltage_cutoff_mv / 1000.0f) : 0.0f;
    float overvoltage_v = (data->overvoltage_cutoff_mv > 0U) ? ((float)data->overvoltage_cutoff_mv / 1000.0f) : 0.0f;
    float max_temp_c = fmaxf(data->mosfet_temperature_c, data->pack_temperature_max_c);
    float min_temp_c = fminf(data->mosfet_temperature_c, data->pack_temperature_min_c);
    float overheat_cutoff_c = (data->overheat_cutoff_c > 0.0f) ? data->overheat_cutoff_c : 65.0f;

    uint8_t byte0 = 0U;
    uint8_t byte1 = 0U;
    uint8_t highest_level = 0U;

    uint8_t underv_level = 0U;
    if (undervoltage_v > 0.0f) {
        if (pack_voltage_v <= undervoltage_v) {
            underv_level = 2U;
        } else if (pack_voltage_v <= (undervoltage_v * 1.05f)) {
            underv_level = 1U;
        }
    }
    byte0 = encode_2bit_field(byte0, 0U, underv_level);
    highest_level = (underv_level > highest_level) ? underv_level : highest_level;

    uint8_t overvoltage_level = 0U;
    if (overvoltage_v > 0.0f) {
        if (pack_voltage_v >= overvoltage_v) {
            overvoltage_level = 2U;
        } else if (pack_voltage_v >= (overvoltage_v * 0.95f)) {
            overvoltage_level = 1U;
        }
    }
    byte0 = encode_2bit_field(byte0, 1U, overvoltage_level);
    highest_level = (overvoltage_level > highest_level) ? overvoltage_level : highest_level;

    uint8_t high_temp_level = 0U;
    if (max_temp_c > overheat_cutoff_c) {
        high_temp_level = 2U;
    } else if (max_temp_c > (overheat_cutoff_c * 0.9f)) {
        high_temp_level = 1U;
    }
    byte0 = encode_2bit_field(byte0, 2U, high_temp_level);
    highest_level = (high_temp_level > highest_level) ? high_temp_level : highest_level;

    uint8_t low_temp_level = 0U;
    if (min_temp_c < -10.0f) {
        low_temp_level = 2U;
    } else if (min_temp_c < 0.0f) {
        low_temp_level = 1U;
    }
    byte0 = encode_2bit_field(byte0, 3U, low_temp_level);
    highest_level = (low_temp_level > highest_level) ? low_temp_level : highest_level;

    uint16_t imbalance_mv = 0U;
    if (data->max_cell_mv > data->min_cell_mv) {
        imbalance_mv = (uint16_t)(data->max_cell_mv - data->min_cell_mv);
    }
    uint8_t imbalance_level = 0U;
    if (imbalance_mv >= 80U) {
        imbalance_level = 2U;
    } else if (imbalance_mv >= 40U) {
        imbalance_level = 1U;
    }
    byte1 = encode_2bit_field(byte1, 0U, imbalance_level);
    highest_level = (imbalance_level > highest_level) ? imbalance_level : highest_level;

    uint8_t low_soc_level = 0U;
    if (data->state_of_charge_pct <= 5.0f) {
        low_soc_level = 2U;
    } else if (data->state_of_charge_pct <= 15.0f) {
        low_soc_level = 1U;
    }
    byte1 = encode_2bit_field(byte1, 1U, low_soc_level);
    highest_level = (low_soc_level > highest_level) ? low_soc_level : highest_level;

    uint8_t high_soc_level = 0U;
    if (data->state_of_charge_pct >= 98.0f && data->pack_current_a > 1.0f) {
        high_soc_level = 1U;
    }
    byte1 = encode_2bit_field(byte1, 2U, high_soc_level);
    highest_level = (high_soc_level > highest_level) ? high_soc_level : highest_level;

    frame->data[0] = byte0;
    frame->data[1] = byte1;
    frame->data[7] = (highest_level >= 2U) ? 0x02U : ((highest_level == 1U) ? 0x01U : 0x00U);

    return true;
}

static bool encode_ascii_field(const uart_bms_live_data_t *data,
                               const char *fallback,
                               uint16_t base_address,
                               size_t offset,
                               can_publisher_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    const char *resolved = NULL;
    char buffer[33];
    if (decode_ascii_from_registers(data, base_address, sizeof(buffer) - 1U, buffer, sizeof(buffer))) {
        resolved = buffer;
    } else {
        resolved = fallback;
    }

    size_t length = (frame->dlc <= sizeof(frame->data)) ? frame->dlc : sizeof(frame->data);
    copy_ascii_padded(frame->data, length, resolved, offset);
    return true;
}

static bool encode_energy_counters(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    update_energy_counters(data);

    memset(frame->data, 0, sizeof(frame->data));

    uint32_t energy_in_raw = encode_energy_wh(s_energy_charged_wh);
    uint32_t energy_out_raw = encode_energy_wh(s_energy_discharged_wh);

    frame->data[0] = (uint8_t)(energy_in_raw & 0xFFU);
    frame->data[1] = (uint8_t)((energy_in_raw >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)((energy_in_raw >> 16U) & 0xFFU);
    frame->data[3] = (uint8_t)((energy_in_raw >> 24U) & 0xFFU);
    frame->data[4] = (uint8_t)(energy_out_raw & 0xFFU);
    frame->data[5] = (uint8_t)((energy_out_raw >> 8U) & 0xFFU);
    frame->data[6] = (uint8_t)((energy_out_raw >> 16U) & 0xFFU);
    frame->data[7] = (uint8_t)((energy_out_raw >> 24U) & 0xFFU);

    return true;
}

static bool encode_installed_capacity(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    float capacity_ah = data->battery_capacity_ah;
    if (capacity_ah <= 0.0f && data->series_cell_count > 0U) {
        capacity_ah = (float)data->series_cell_count * 2.5f;
    }

    if (data->state_of_health_pct > 0.0f) {
        capacity_ah *= data->state_of_health_pct / 100.0f;
    }

    if (capacity_ah < 0.0f) {
        capacity_ah = 0.0f;
    }

    uint16_t raw_capacity = encode_u16_scaled(capacity_ah, 1.0f, 0.0f, 0U, 0xFFFFU);

    frame->data[0] = (uint8_t)(raw_capacity & 0xFFU);
    frame->data[1] = (uint8_t)((raw_capacity >> 8U) & 0xFFU);

    return true;
}

static bool encode_manufacturer_string(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_manufacturer_string(data);
    return encode_ascii_field(data, resolved, 0x01F4U, 0U, frame);
}

static bool encode_battery_name(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_name_string(data);
    return encode_ascii_field(data, resolved, 0x01F6U, 0U, frame);
}

static bool encode_battery_name_part2(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_name_string(data);
    return encode_ascii_field(data, resolved, 0x01F6U, 8U, frame);
}

static bool encode_battery_family(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_family_string(data);
    return encode_ascii_field(data, resolved, 0x01F6U, 0U, frame);
}

const can_publisher_channel_t g_can_publisher_channels[] = {
    {
        .pgn = VICTRON_PGN_CVL_CCL_DCL,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_CVL_CCL_DCL),
        .dlc = 8,
        .fill_fn = encode_charge_limits,
        .description = "Victron charge/discharge limits",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_SOC_SOH,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_SOC_SOH),
        .dlc = 8,
        .fill_fn = encode_soc_soh,
        .description = "Victron SOC/SOH",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_VOLTAGE_CURRENT,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_VOLTAGE_CURRENT),
        .dlc = 8,
        .fill_fn = encode_voltage_current_temperature,
        .description = "Victron voltage/current/temperature",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_ALARMS,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_ALARMS),
        .dlc = 8,
        .fill_fn = encode_alarm_status,
        .description = "Victron alarm summary",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_MANUFACTURER,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MANUFACTURER),
        .dlc = 8,
        .fill_fn = encode_manufacturer_string,
        .description = "Victron manufacturer string",
        .period_ms = 2000U,
    },
    {
        .pgn = VICTRON_PGN_BATTERY_INFO,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_BATTERY_INFO),
        .dlc = 8,
        .fill_fn = encode_battery_name,
        .description = "Victron battery info",
        .period_ms = 2000U,
    },
    {
        .pgn = VICTRON_PGN_BMS_NAME_PART2,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_BMS_NAME_PART2),
        .dlc = 8,
        .fill_fn = encode_battery_name_part2,
        .description = "Victron battery info part 2",
        .period_ms = 2000U,
    },
    {
        .pgn = VICTRON_PGN_ENERGY_COUNTERS,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_ENERGY_COUNTERS),
        .dlc = 8,
        .fill_fn = encode_energy_counters,
        .description = "Victron energy counters",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_INSTALLED_CAP,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_INSTALLED_CAP),
        .dlc = 8,
        .fill_fn = encode_installed_capacity,
        .description = "Victron installed capacity",
        .period_ms = 5000U,
    },
    {
        .pgn = VICTRON_PGN_BATTERY_FAMILY,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_BATTERY_FAMILY),
        .dlc = 8,
        .fill_fn = encode_battery_family,
        .description = "Victron battery family",
        .period_ms = 5000U,
    },
};

const size_t g_can_publisher_channel_count =
    sizeof(g_can_publisher_channels) / sizeof(g_can_publisher_channels[0]);
