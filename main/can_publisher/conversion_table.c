#include "conversion_table.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "config_manager.h"
#include "cvl_controller.h"
#include "storage/nvs_energy.h"
#include "can_config_defaults.h"

#define VICTRON_CAN_HANDSHAKE_ID     0x307U
#define VICTRON_PGN_CVL_CCL_DCL      0x351U
#define VICTRON_PGN_SOC_SOH          0x355U
#define VICTRON_PGN_VOLTAGE_CURRENT  0x356U
#define VICTRON_PGN_ALARMS           0x35AU
#define VICTRON_PGN_MANUFACTURER     0x35EU
#define VICTRON_PGN_BATTERY_INFO     0x35FU
#define VICTRON_PGN_BMS_NAME_PART1   0x370U
#define VICTRON_PGN_BMS_NAME_PART2   0x371U
#define VICTRON_PGN_MODULE_STATUS    0x372U
#define VICTRON_PGN_CELL_EXTREMES    0x373U
#define VICTRON_PGN_MIN_CELL_ID      0x374U
#define VICTRON_PGN_MAX_CELL_ID      0x375U
#define VICTRON_PGN_MIN_TEMP_ID      0x376U
#define VICTRON_PGN_MAX_TEMP_ID      0x377U
#define VICTRON_PGN_ENERGY_COUNTERS  0x378U
#define VICTRON_PGN_INSTALLED_CAP    0x379U
#define VICTRON_PGN_SERIAL_PART1     0x380U
#define VICTRON_PGN_SERIAL_PART2     0x381U
#define VICTRON_PGN_BATTERY_FAMILY   0x382U

#define VICTRON_PRIORITY             6U
#define VICTRON_SOURCE_ADDRESS       0xE5U
#define VICTRON_EXTENDED_ID(pgn) \
    ((((uint32_t)VICTRON_PRIORITY) << 26) | ((uint32_t)(pgn) << 8) | (uint32_t)VICTRON_SOURCE_ADDRESS)

// CAN configuration defaults are now centralized in can_config_defaults.h

#ifndef CONFIG_TINYBMS_CAN_SERIAL_NUMBER
#define CONFIG_TINYBMS_CAN_SERIAL_NUMBER "TinyBMS-00000000"
#endif

static const char *TAG = "can_conv";

static double s_energy_charged_wh = 0.0;
static double s_energy_discharged_wh = 0.0;
static double s_energy_last_persist_charged_wh = 0.0;
static double s_energy_last_persist_discharged_wh = 0.0;
static uint64_t s_energy_last_timestamp_ms = 0;
static uint64_t s_energy_last_persist_ms = 0;
static bool s_energy_dirty = false;
static bool s_energy_storage_ready = false;

static const config_manager_can_settings_t *conversion_get_can_settings(void)
{
    static const config_manager_can_settings_t defaults = {
        .twai = {
            .tx_gpio = CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO,
            .rx_gpio = CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO,
        },
        .keepalive = {
            .interval_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS,
            .timeout_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS,
            .retry_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS,
        },
        .publisher = {
            .period_ms = CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS,
        },
        .identity = {
            .handshake_ascii = CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII,
            .manufacturer = CONFIG_TINYBMS_CAN_MANUFACTURER,
            .battery_name = CONFIG_TINYBMS_CAN_BATTERY_NAME,
            .battery_family = CONFIG_TINYBMS_CAN_BATTERY_FAMILY,
            .serial_number = CONFIG_TINYBMS_CAN_SERIAL_NUMBER,
        },
    };

    const config_manager_can_settings_t *settings = config_manager_get_can_settings();
    return (settings != NULL) ? settings : &defaults;
}

#define ENERGY_PERSIST_MIN_DELTA_WH   10.0
#define ENERGY_PERSIST_INTERVAL_MS    60000U

#define TINY_REGISTER_BATTERY_CAPACITY 0x0132U
#define TINY_REGISTER_HARDWARE_VERSION 0x01F4U
#define TINY_REGISTER_PUBLIC_FIRMWARE  0x01F5U
#define TINY_REGISTER_INTERNAL_FW      0x01F6U
#define TINY_REGISTER_SERIAL_NUMBER    0x01FAU
#define TINY_REGISTER_BATTERY_FAMILY   0x01F8U

static bool ensure_energy_storage_ready(void)
{
    if (s_energy_storage_ready) {
        return true;
    }

    esp_err_t err = nvs_energy_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialise energy storage: %s", esp_err_to_name(err));
        return false;
    }

    s_energy_storage_ready = true;
    return true;
}

static void set_energy_state_internal(double charged_wh, double discharged_wh, bool persisted)
{
    if (!(charged_wh > 0.0) || !isfinite(charged_wh)) {
        charged_wh = 0.0;
    }
    if (!(discharged_wh > 0.0) || !isfinite(discharged_wh)) {
        discharged_wh = 0.0;
    }

    s_energy_charged_wh = charged_wh;
    s_energy_discharged_wh = discharged_wh;
    s_energy_last_timestamp_ms = 0;

    if (persisted) {
        s_energy_last_persist_charged_wh = charged_wh;
        s_energy_last_persist_discharged_wh = discharged_wh;
        s_energy_dirty = false;
    } else {
        s_energy_dirty = true;
    }
}

static esp_err_t persist_energy_state_internal(void)
{
    if (!ensure_energy_storage_ready()) {
        return ESP_FAIL;
    }

    nvs_energy_state_t state = {
        .charged_wh = (s_energy_charged_wh > 0.0) ? s_energy_charged_wh : 0.0,
        .discharged_wh = (s_energy_discharged_wh > 0.0) ? s_energy_discharged_wh : 0.0,
    };

    esp_err_t err = nvs_energy_store(&state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist energy counters: %s", esp_err_to_name(err));
        return err;
    }

    s_energy_last_persist_charged_wh = state.charged_wh;
    s_energy_last_persist_discharged_wh = state.discharged_wh;
    s_energy_dirty = false;
    return ESP_OK;
}

void can_publisher_conversion_reset_state(void)
{
    set_energy_state_internal(0.0, 0.0, true);
    s_energy_last_persist_ms = 0;

    if (ensure_energy_storage_ready()) {
        esp_err_t err = nvs_energy_clear();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear stored energy counters: %s", esp_err_to_name(err));
        }
    }
}

void can_publisher_conversion_set_energy_state(double charged_wh, double discharged_wh)
{
    set_energy_state_internal(charged_wh, discharged_wh, false);
}

void can_publisher_conversion_get_energy_state(double *charged_wh, double *discharged_wh)
{
    if (charged_wh != NULL) {
        *charged_wh = s_energy_charged_wh;
    }
    if (discharged_wh != NULL) {
        *discharged_wh = s_energy_discharged_wh;
    }
}

esp_err_t can_publisher_conversion_restore_energy_state(void)
{
    if (!ensure_energy_storage_ready()) {
        return ESP_FAIL;
    }

    nvs_energy_state_t state = {0};
    esp_err_t err = nvs_energy_load(&state);
    if (err == ESP_OK) {
        set_energy_state_internal(state.charged_wh, state.discharged_wh, true);
        s_energy_last_persist_ms = 0;
        ESP_LOGI(TAG,
                 "Restored energy counters charged=%.1f Wh discharged=%.1f Wh",
                 state.charged_wh,
                 state.discharged_wh);
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load energy counters: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t can_publisher_conversion_persist_energy_state(void)
{
    esp_err_t err = persist_energy_state_internal();
    if (err == ESP_OK) {
        s_energy_last_persist_ms = 0;
    }
    return err;
}

static void maybe_persist_energy(uint64_t timestamp_ms)
{
    if (!s_energy_dirty || timestamp_ms == 0U) {
        return;
    }

    if (s_energy_last_persist_ms != 0U) {
        if (timestamp_ms <= s_energy_last_persist_ms) {
            return;
        }
        uint64_t elapsed = timestamp_ms - s_energy_last_persist_ms;
        if (elapsed < ENERGY_PERSIST_INTERVAL_MS) {
            return;
        }
    }

    double delta_in = fabs(s_energy_charged_wh - s_energy_last_persist_charged_wh);
    double delta_out = fabs(s_energy_discharged_wh - s_energy_last_persist_discharged_wh);
    if (delta_in < ENERGY_PERSIST_MIN_DELTA_WH && delta_out < ENERGY_PERSIST_MIN_DELTA_WH) {
        return;
    }

    if (persist_energy_state_internal() == ESP_OK) {
        s_energy_last_persist_ms = timestamp_ms;
    }
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

static uint8_t level_from_high_threshold(float value, float warn_threshold, float alarm_threshold)
{
    if (!isfinite(value) || !isfinite(alarm_threshold) || alarm_threshold <= 0.0f) {
        return 0U;
    }
    if (!isfinite(warn_threshold) || warn_threshold <= 0.0f || warn_threshold > alarm_threshold) {
        warn_threshold = alarm_threshold;
    }
    if (value >= alarm_threshold) {
        return 2U;
    }
    if (value >= warn_threshold) {
        return 1U;
    }
    return 0U;
}

static uint8_t level_from_low_threshold(float value, float warn_threshold, float alarm_threshold)
{
    if (!isfinite(value) || !isfinite(alarm_threshold)) {
        return 0U;
    }
    if (!isfinite(warn_threshold) || warn_threshold < alarm_threshold) {
        warn_threshold = alarm_threshold;
    }
    if (value <= alarm_threshold) {
        return 2U;
    }
    if (value <= warn_threshold) {
        return 1U;
    }
    return 0U;
}

static uint8_t alarm_field_value(uint8_t level)
{
    return (level >= 2U) ? 2U : 0U;
}

static uint8_t warning_field_value(uint8_t level)
{
    if (level >= 2U) {
        return 2U;
    }
    if (level == 1U) {
        return 1U;
    }
    return 0U;
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

static bool encode_battery_identification(const uart_bms_live_data_t *data,
                                          can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    uint16_t model_id = (uint16_t)data->hardware_version |
                        (uint16_t)((uint16_t)data->hardware_changes_version << 8U);
    if (model_id == 0U) {
        (void)find_register_value(data, TINY_REGISTER_HARDWARE_VERSION, &model_id);
    }

    uint16_t firmware_word = (uint16_t)data->firmware_version |
                              (uint16_t)((uint16_t)data->firmware_flags << 8U);
    if (firmware_word == 0U) {
        (void)find_register_value(data, TINY_REGISTER_PUBLIC_FIRMWARE, &firmware_word);
    }

    float capacity_ah = data->battery_capacity_ah;
    if (!(capacity_ah > 0.0f)) {
        capacity_ah = 0.0f;
    }
    uint16_t capacity_word = encode_u16_scaled(capacity_ah, 100.0f, 0.0f, 0U, 0xFFFFU);

    frame->data[0] = (uint8_t)(model_id & 0xFFU);
    frame->data[1] = (uint8_t)((model_id >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(firmware_word & 0xFFU);
    frame->data[3] = (uint8_t)((firmware_word >> 8U) & 0xFFU);
    frame->data[4] = (uint8_t)(capacity_word & 0xFFU);
    frame->data[5] = (uint8_t)((capacity_word >> 8U) & 0xFFU);

    uint16_t internal_fw = data->internal_firmware_version;
    if (internal_fw == 0U) {
        (void)find_register_value(data, TINY_REGISTER_INTERNAL_FW, &internal_fw);
    }

    frame->data[6] = (uint8_t)(internal_fw & 0xFFU);
    frame->data[7] = (uint8_t)((internal_fw >> 8U) & 0xFFU);

    return true;
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

    double delta_in = fabs(s_energy_charged_wh - s_energy_last_persist_charged_wh);
    double delta_out = fabs(s_energy_discharged_wh - s_energy_last_persist_discharged_wh);
    if (delta_in >= ENERGY_PERSIST_MIN_DELTA_WH || delta_out >= ENERGY_PERSIST_MIN_DELTA_WH) {
        s_energy_dirty = true;
    }

    maybe_persist_energy(current_ts);
}

static const char *resolve_manufacturer_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, 0x01F4U, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    const config_manager_can_settings_t *settings = conversion_get_can_settings();
    if (settings != NULL && settings->identity.manufacturer[0] != '\0') {
        return settings->identity.manufacturer;
    }

    return CONFIG_TINYBMS_CAN_MANUFACTURER;
}

static const char *resolve_battery_name_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, 0x01F6U, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    const config_manager_can_settings_t *settings = conversion_get_can_settings();
    if (settings != NULL && settings->identity.battery_name[0] != '\0') {
        return settings->identity.battery_name;
    }

    return CONFIG_TINYBMS_CAN_BATTERY_NAME;
}

static const char *resolve_serial_number_string(const uart_bms_live_data_t *data)
{
    static char buffer[UART_BMS_SERIAL_NUMBER_MAX_LENGTH + 1];

    if (data != NULL) {
        size_t length = (size_t)data->serial_length;
        if (length == 0U && data->serial_number[0] != '\0') {
            length = strnlen(data->serial_number, sizeof(data->serial_number));
        }
        if (length > 0U) {
            if (length >= sizeof(buffer)) {
                length = sizeof(buffer) - 1U;
            }
            memcpy(buffer, data->serial_number, length);
            buffer[length] = '\0';
            return buffer;
        }
    }

    const config_manager_can_settings_t *settings = conversion_get_can_settings();
    if (settings != NULL && settings->identity.serial_number[0] != '\0') {
        return settings->identity.serial_number;
    }

    return CONFIG_TINYBMS_CAN_SERIAL_NUMBER;
}

static const char *resolve_battery_family_string(const uart_bms_live_data_t *data)
{
    static char buffer[17];

    if (decode_ascii_from_registers(data, TINY_REGISTER_BATTERY_FAMILY, 16U, buffer, sizeof(buffer))) {
        return buffer;
    }

    const config_manager_can_settings_t *settings = conversion_get_can_settings();
    if (settings != NULL && settings->identity.battery_family[0] != '\0') {
        return settings->identity.battery_family;
    }

    return CONFIG_TINYBMS_CAN_BATTERY_FAMILY;
}

static bool encode_inverter_identifier(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    uint16_t model_id = (uint16_t)data->hardware_version |
                        (uint16_t)((uint16_t)data->hardware_changes_version << 8U);
    if (model_id == 0U) {
        (void)find_register_value(data, TINY_REGISTER_HARDWARE_VERSION, &model_id);
    }

    uint16_t firmware_word = (uint16_t)data->firmware_version |
                              (uint16_t)((uint16_t)data->firmware_flags << 8U);
    if (firmware_word == 0U) {
        (void)find_register_value(data, TINY_REGISTER_PUBLIC_FIRMWARE, &firmware_word);
    }

    frame->data[0] = (uint8_t)(model_id & 0xFFU);
    frame->data[1] = (uint8_t)((model_id >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(firmware_word & 0xFFU);
    frame->data[3] = (uint8_t)((firmware_word >> 8U) & 0xFFU);

    const char *handshake_ascii = CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII;
    const config_manager_can_settings_t *settings = conversion_get_can_settings();
    if (settings != NULL && settings->identity.handshake_ascii[0] != '\0') {
        handshake_ascii = settings->identity.handshake_ascii;
    }

    size_t ascii_length = strnlen(handshake_ascii, CONFIG_MANAGER_CAN_HANDSHAKE_MAX_LENGTH);
    for (size_t i = 0; i < 3U; ++i) {
        uint8_t value = 0U;
        if (i < ascii_length) {
            value = sanitize_ascii((uint8_t)handshake_ascii[i]);
        }
        frame->data[4U + i] = value;
    }

    return true;
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

        ccl_a = sanitize_positive(data->max_charge_current_limit_a);
        if (ccl_a <= 0.0f) {
            ccl_a = sanitize_positive(data->charge_overcurrent_limit_a);
        }
        if (ccl_a <= 0.0f && data->peak_discharge_current_limit_a > 0.0f) {
            ccl_a = sanitize_positive(data->peak_discharge_current_limit_a);
        }

        dcl_a = sanitize_positive(data->max_discharge_current_limit_a);
        if (dcl_a <= 0.0f) {
            dcl_a = sanitize_positive(data->discharge_overcurrent_limit_a);
        }
        if (dcl_a <= 0.0f && data->peak_discharge_current_limit_a > 0.0f) {
            dcl_a = sanitize_positive(data->peak_discharge_current_limit_a);
        }
    }

    uint16_t cvl_raw = encode_u16_scaled(cvl_v, 10.0f, 0.0f, 0U, 0xFFFFU);
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

    uint16_t soc_raw = encode_u16_scaled(data->state_of_charge_pct, 1.0f, 0.0f, 0U, 100U);
    uint16_t soh_raw = encode_u16_scaled(data->state_of_health_pct, 1.0f, 0.0f, 0U, 100U);

    frame->data[0] = (uint8_t)(soc_raw & 0xFFU);
    frame->data[1] = (uint8_t)((soc_raw >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(soh_raw & 0xFFU);
    frame->data[3] = (uint8_t)((soh_raw >> 8U) & 0xFFU);

    uint16_t soc_register_words[2] = {0};
    if (read_register_block(data, 0x002EU, 2U, soc_register_words) == 2U) {
        uint32_t soc_register_raw = (uint32_t)soc_register_words[0] |
                                    ((uint32_t)soc_register_words[1] << 16U);
        double high_res_scaled = (double)soc_register_raw * 0.0001;
        long high_res_rounded = lrint(high_res_scaled);

        uint16_t high_res_value = 0U;
        if (high_res_rounded <= 0L) {
            high_res_value = 0U;
        } else if (high_res_rounded >= 10000L) {
            high_res_value = 10000U;
        } else {
            high_res_value = (uint16_t)high_res_rounded;
        }

        frame->data[4] = (uint8_t)(high_res_value & 0xFFU);
        frame->data[5] = (uint8_t)((high_res_value >> 8U) & 0xFFU);
    }

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

    uint8_t bytes[8] = {0};
    uint8_t highest_level = 0U;

    const float pack_voltage_v = data->pack_voltage_v;
    const float undervoltage_v = (data->undervoltage_cutoff_mv > 0U)
                                     ? ((float)data->undervoltage_cutoff_mv / 1000.0f)
                                     : 0.0f;
    const float overvoltage_v = (data->overvoltage_cutoff_mv > 0U)
                                    ? ((float)data->overvoltage_cutoff_mv / 1000.0f)
                                    : 0.0f;
    const float max_temp_c = fmaxf(data->mosfet_temperature_c, data->pack_temperature_max_c);
    const float min_temp_c = fminf(data->mosfet_temperature_c, data->pack_temperature_min_c);
    const float overheat_cutoff_c = (data->overheat_cutoff_c > 0.0f) ? data->overheat_cutoff_c : 65.0f;
    const float external_temp_c = data->auxiliary_temperature_c;
    const float low_temp_charge_cutoff_c = data->low_temp_charge_cutoff_c;
    const float discharge_limit_a = data->discharge_overcurrent_limit_a;
    const float charge_limit_a = data->charge_overcurrent_limit_a;
    const float discharge_current_a = (data->pack_current_a < 0.0f) ? -data->pack_current_a : 0.0f;
    const float charge_current_a = (data->pack_current_a > 0.0f) ? data->pack_current_a : 0.0f;

    uint8_t level = level_from_low_threshold(pack_voltage_v,
                                             undervoltage_v * 1.05f,
                                             undervoltage_v);
    highest_level = (level > highest_level) ? level : highest_level;
    bytes[0] = encode_2bit_field(bytes[0], 2U, alarm_field_value(level));
    bytes[4] = encode_2bit_field(bytes[4], 2U, warning_field_value(level));

    level = level_from_high_threshold(pack_voltage_v,
                                      overvoltage_v * 0.95f,
                                      overvoltage_v);
    highest_level = (level > highest_level) ? level : highest_level;
    bytes[0] = encode_2bit_field(bytes[0], 1U, alarm_field_value(level));
    bytes[4] = encode_2bit_field(bytes[4], 1U, warning_field_value(level));

    level = level_from_high_threshold(max_temp_c,
                                      overheat_cutoff_c * 0.9f,
                                      overheat_cutoff_c);
    highest_level = (level > highest_level) ? level : highest_level;
    bytes[0] = encode_2bit_field(bytes[0], 3U, alarm_field_value(level));
    bytes[4] = encode_2bit_field(bytes[4], 3U, warning_field_value(level));

    level = level_from_low_threshold(min_temp_c, 0.0f, -10.0f);
    highest_level = (level > highest_level) ? level : highest_level;
    bytes[1] = encode_2bit_field(bytes[1], 0U, alarm_field_value(level));
    bytes[5] = encode_2bit_field(bytes[5], 0U, warning_field_value(level));

    uint8_t high_temp_charge_level = 0U;
    if (isfinite(external_temp_c)) {
        high_temp_charge_level = level_from_high_threshold(external_temp_c,
                                                          overheat_cutoff_c * 0.9f,
                                                          overheat_cutoff_c);
    }
    highest_level = (high_temp_charge_level > highest_level) ? high_temp_charge_level : highest_level;
    bytes[1] = encode_2bit_field(bytes[1], 1U, alarm_field_value(high_temp_charge_level));
    bytes[5] = encode_2bit_field(bytes[5], 1U, warning_field_value(high_temp_charge_level));

    uint8_t low_temp_charge_warning_level = 0U;
    if (isfinite(external_temp_c)) {
        low_temp_charge_warning_level = level_from_low_threshold(external_temp_c,
                                                                 low_temp_charge_cutoff_c + 5.0f,
                                                                 low_temp_charge_cutoff_c);
    }
    highest_level = (low_temp_charge_warning_level > highest_level) ? low_temp_charge_warning_level : highest_level;
    bytes[5] = encode_2bit_field(bytes[5], 2U, warning_field_value(low_temp_charge_warning_level));

    uint8_t high_current_level = 0U;
    if (discharge_limit_a > 0.0f) {
        high_current_level = level_from_high_threshold(discharge_current_a,
                                                       discharge_limit_a * 0.8f,
                                                       discharge_limit_a);
    }
    highest_level = (high_current_level > highest_level) ? high_current_level : highest_level;
    bytes[1] = encode_2bit_field(bytes[1], 3U, alarm_field_value(high_current_level));
    bytes[5] = encode_2bit_field(bytes[5], 3U, warning_field_value(high_current_level));

    uint8_t high_charge_current_level = 0U;
    if (charge_limit_a > 0.0f) {
        high_charge_current_level = level_from_high_threshold(charge_current_a,
                                                              charge_limit_a * 0.8f,
                                                              charge_limit_a);
    }
    highest_level = (high_charge_current_level > highest_level) ? high_charge_current_level : highest_level;
    bytes[2] = encode_2bit_field(bytes[2], 0U, alarm_field_value(high_charge_current_level));
    bytes[6] = encode_2bit_field(bytes[6], 0U, warning_field_value(high_charge_current_level));

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
    highest_level = (imbalance_level > highest_level) ? imbalance_level : highest_level;
    bytes[3] = encode_2bit_field(bytes[3], 0U, alarm_field_value(imbalance_level));
    bytes[7] = encode_2bit_field(bytes[7], 0U, warning_field_value(imbalance_level));

    bytes[0] = encode_2bit_field(bytes[0], 0U, (highest_level >= 2U) ? 2U : 0U);
    bytes[4] = encode_2bit_field(bytes[4], 0U, warning_field_value(highest_level));

    bytes[1] = encode_2bit_field(bytes[1], 2U, 0x3U);
    bytes[2] = encode_2bit_field(bytes[2], 1U, 0x3U);
    bytes[2] = encode_2bit_field(bytes[2], 2U, 0x3U);
    bytes[2] = encode_2bit_field(bytes[2], 3U, 0x3U);
    bytes[3] = encode_2bit_field(bytes[3], 1U, 0x3U);
    bytes[3] = encode_2bit_field(bytes[3], 2U, 0x3U);
    bytes[3] = encode_2bit_field(bytes[3], 3U, 0x3U);
    bytes[6] = encode_2bit_field(bytes[6], 1U, 0x3U);
    bytes[6] = encode_2bit_field(bytes[6], 2U, 0x3U);
    bytes[6] = encode_2bit_field(bytes[6], 3U, 0x3U);
    bytes[7] = encode_2bit_field(bytes[7], 2U, 0x3U);
    bytes[7] = encode_2bit_field(bytes[7], 3U, 0x3U);

    for (size_t i = 0; i < sizeof(bytes); ++i) {
        frame->data[i] = bytes[i];
    }

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

static bool encode_battery_name_part1(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_name_string(data);
    return encode_ascii_field(data, resolved, 0x01F6U, 0U, frame);
}

static bool encode_module_status_counts(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    bool offline = (data->timestamp_ms == 0U);
    float charge_limit = sanitize_positive(data->max_charge_current_limit_a);
    float discharge_limit = sanitize_positive(data->max_discharge_current_limit_a);

    uint16_t modules_ok = offline ? 0U : 1U;
    uint16_t blocking_charge = (charge_limit <= 0.0f) ? 1U : 0U;
    uint16_t blocking_discharge = (discharge_limit <= 0.0f) ? 1U : 0U;
    uint16_t offline_count = offline ? 1U : 0U;

    if (data->warning_bits != 0U && blocking_charge == 0U) {
        blocking_charge = 1U;
    }
    if (data->alarm_bits != 0U && blocking_discharge == 0U) {
        blocking_discharge = 1U;
    }

    if (offline_count > 0U) {
        modules_ok = 0U;
    }

    frame->data[0] = (uint8_t)(modules_ok & 0xFFU);
    frame->data[1] = (uint8_t)((modules_ok >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(blocking_charge & 0xFFU);
    frame->data[3] = (uint8_t)((blocking_charge >> 8U) & 0xFFU);
    frame->data[4] = (uint8_t)(blocking_discharge & 0xFFU);
    frame->data[5] = (uint8_t)((blocking_discharge >> 8U) & 0xFFU);
    frame->data[6] = (uint8_t)(offline_count & 0xFFU);
    frame->data[7] = (uint8_t)((offline_count >> 8U) & 0xFFU);

    return true;
}

static bool encode_cell_voltage_temperature_extremes(const uart_bms_live_data_t *data,
                                                     can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    memset(frame->data, 0, sizeof(frame->data));

    uint16_t min_mv = data->min_cell_mv;
    uint16_t max_mv = data->max_cell_mv;

    uint16_t min_k = encode_u16_scaled(data->pack_temperature_min_c, 1.0f, 273.15f, 0U, 0xFFFFU);
    uint16_t max_k = encode_u16_scaled(data->pack_temperature_max_c, 1.0f, 273.15f, 0U, 0xFFFFU);

    frame->data[0] = (uint8_t)(min_mv & 0xFFU);
    frame->data[1] = (uint8_t)((min_mv >> 8U) & 0xFFU);
    frame->data[2] = (uint8_t)(max_mv & 0xFFU);
    frame->data[3] = (uint8_t)((max_mv >> 8U) & 0xFFU);
    frame->data[4] = (uint8_t)(min_k & 0xFFU);
    frame->data[5] = (uint8_t)((min_k >> 8U) & 0xFFU);
    frame->data[6] = (uint8_t)(max_k & 0xFFU);
    frame->data[7] = (uint8_t)((max_k >> 8U) & 0xFFU);

    return true;
}

static void encode_identifier_string(const char *text, can_publisher_frame_t *frame)
{
    memset(frame->data, 0, sizeof(frame->data));
    copy_ascii_padded(frame->data, frame->dlc, text, 0U);
}

static bool encode_min_cell_identifier(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    char buffer[9] = {0};
    unsigned value = data->min_cell_mv;
    if (value > 9999U) {
        value = 9999U;
    }
    (void)snprintf(buffer, sizeof(buffer), "MINV%04u", value);
    encode_identifier_string(buffer, frame);
    return true;
}

static bool encode_max_cell_identifier(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    char buffer[9] = {0};
    unsigned value = data->max_cell_mv;
    if (value > 9999U) {
        value = 9999U;
    }
    (void)snprintf(buffer, sizeof(buffer), "MAXV%04u", value);
    encode_identifier_string(buffer, frame);
    return true;
}

static int clamp_temperature_identifier(float value_c)
{
    if (!isfinite(value_c)) {
        return 0;
    }
    long rounded = lrintf(value_c);
    if (rounded < -999L) {
        rounded = -999L;
    }
    if (rounded > 999L) {
        rounded = 999L;
    }
    return (int)rounded;
}

static bool encode_min_temp_identifier(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    char buffer[9] = {0};
    int temp_c = clamp_temperature_identifier(data->pack_temperature_min_c);
    (void)snprintf(buffer, sizeof(buffer), "MINT%+04d", temp_c);
    encode_identifier_string(buffer, frame);
    return true;
}

static bool encode_max_temp_identifier(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    if (data == NULL || frame == NULL) {
        return false;
    }

    char buffer[9] = {0};
    int temp_c = clamp_temperature_identifier(data->pack_temperature_max_c);
    (void)snprintf(buffer, sizeof(buffer), "MAXT%+04d", temp_c);
    encode_identifier_string(buffer, frame);
    return true;
}

static bool encode_serial_number_part1(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_serial_number_string(data);
    return encode_ascii_field(data, resolved, TINY_REGISTER_SERIAL_NUMBER, 0U, frame);
}

static bool encode_serial_number_part2(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_serial_number_string(data);
    return encode_ascii_field(data, resolved, TINY_REGISTER_SERIAL_NUMBER, 8U, frame);
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

static bool encode_battery_name_part2(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_name_string(data);
    return encode_ascii_field(data, resolved, 0x01F6U, 8U, frame);
}

static bool encode_battery_family(const uart_bms_live_data_t *data, can_publisher_frame_t *frame)
{
    const char *resolved = resolve_battery_family_string(data);
    return encode_ascii_field(data, resolved, TINY_REGISTER_BATTERY_FAMILY, 0U, frame);
}

const can_publisher_channel_t g_can_publisher_channels[] = {
    {
        .pgn = VICTRON_CAN_HANDSHAKE_ID,
        .can_id = VICTRON_CAN_HANDSHAKE_ID,
        .dlc = 8,
        .fill_fn = encode_inverter_identifier,
        .description = "Victron inverter identifier handshake",
        .period_ms = 1000U,
    },
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
        .fill_fn = encode_battery_identification,
        .description = "Victron battery identification",
        .period_ms = 2000U,
    },
    {
        .pgn = VICTRON_PGN_BMS_NAME_PART1,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_BMS_NAME_PART1),
        .dlc = 8,
        .fill_fn = encode_battery_name_part1,
        .description = "Victron battery info part 1",
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
        .pgn = VICTRON_PGN_MODULE_STATUS,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MODULE_STATUS),
        .dlc = 8,
        .fill_fn = encode_module_status_counts,
        .description = "Victron module status counts",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_CELL_EXTREMES,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_CELL_EXTREMES),
        .dlc = 8,
        .fill_fn = encode_cell_voltage_temperature_extremes,
        .description = "Victron cell voltage & temperature extremes",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_MIN_CELL_ID,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MIN_CELL_ID),
        .dlc = 8,
        .fill_fn = encode_min_cell_identifier,
        .description = "Victron min cell identifier",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_MAX_CELL_ID,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MAX_CELL_ID),
        .dlc = 8,
        .fill_fn = encode_max_cell_identifier,
        .description = "Victron max cell identifier",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_MIN_TEMP_ID,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MIN_TEMP_ID),
        .dlc = 8,
        .fill_fn = encode_min_temp_identifier,
        .description = "Victron min temperature identifier",
        .period_ms = 1000U,
    },
    {
        .pgn = VICTRON_PGN_MAX_TEMP_ID,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_MAX_TEMP_ID),
        .dlc = 8,
        .fill_fn = encode_max_temp_identifier,
        .description = "Victron max temperature identifier",
        .period_ms = 1000U,
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
        .pgn = VICTRON_PGN_SERIAL_PART1,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_SERIAL_PART1),
        .dlc = 8,
        .fill_fn = encode_serial_number_part1,
        .description = "Victron serial number part 1",
        .period_ms = 5000U,
    },
    {
        .pgn = VICTRON_PGN_SERIAL_PART2,
        .can_id = VICTRON_EXTENDED_ID(VICTRON_PGN_SERIAL_PART2),
        .dlc = 8,
        .fill_fn = encode_serial_number_part2,
        .description = "Victron serial number part 2",
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
