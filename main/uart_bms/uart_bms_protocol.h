#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file uart_bms_protocol.h
 * @brief Enumerations and metadata describing the TinyBMS UART telemetry
 *        registers handled by the gateway.
 */

/**
 * @brief Number of 16-bit register words requested in each TinyBMS poll.
 */
#define UART_BMS_REGISTER_WORD_COUNT 59

/**
 * @brief Enumerates the logical TinyBMS registers that are polled over UART.
 */
typedef enum {
    UART_BMS_REGISTER_CELL_VOLTAGE_01 = 0,
    UART_BMS_REGISTER_CELL_VOLTAGE_02,
    UART_BMS_REGISTER_CELL_VOLTAGE_03,
    UART_BMS_REGISTER_CELL_VOLTAGE_04,
    UART_BMS_REGISTER_CELL_VOLTAGE_05,
    UART_BMS_REGISTER_CELL_VOLTAGE_06,
    UART_BMS_REGISTER_CELL_VOLTAGE_07,
    UART_BMS_REGISTER_CELL_VOLTAGE_08,
    UART_BMS_REGISTER_CELL_VOLTAGE_09,
    UART_BMS_REGISTER_CELL_VOLTAGE_10,
    UART_BMS_REGISTER_CELL_VOLTAGE_11,
    UART_BMS_REGISTER_CELL_VOLTAGE_12,
    UART_BMS_REGISTER_CELL_VOLTAGE_13,
    UART_BMS_REGISTER_CELL_VOLTAGE_14,
    UART_BMS_REGISTER_CELL_VOLTAGE_15,
    UART_BMS_REGISTER_CELL_VOLTAGE_16,
    UART_BMS_REGISTER_LIFETIME_COUNTER,
    UART_BMS_REGISTER_ESTIMATED_TIME_LEFT,
    UART_BMS_REGISTER_PACK_VOLTAGE,
    UART_BMS_REGISTER_PACK_CURRENT,
    UART_BMS_REGISTER_MIN_CELL_VOLTAGE,
    UART_BMS_REGISTER_MAX_CELL_VOLTAGE,
    UART_BMS_REGISTER_EXTERNAL_TEMPERATURE_1,
    UART_BMS_REGISTER_EXTERNAL_TEMPERATURE_2,
    UART_BMS_REGISTER_STATE_OF_HEALTH,
    UART_BMS_REGISTER_STATE_OF_CHARGE,
    UART_BMS_REGISTER_INTERNAL_TEMPERATURE,
    UART_BMS_REGISTER_SYSTEM_STATUS,
    UART_BMS_REGISTER_NEED_BALANCING,
    UART_BMS_REGISTER_REAL_BALANCING_BITS,
    UART_BMS_REGISTER_MAX_DISCHARGE_CURRENT,
    UART_BMS_REGISTER_MAX_CHARGE_CURRENT,
    UART_BMS_REGISTER_PACK_TEMPERATURE_MIN_MAX,
    UART_BMS_REGISTER_PEAK_DISCHARGE_CURRENT_CUTOFF,
    UART_BMS_REGISTER_BATTERY_CAPACITY,
    UART_BMS_REGISTER_SERIES_CELL_COUNT,
    UART_BMS_REGISTER_OVERVOLTAGE_CUTOFF,
    UART_BMS_REGISTER_UNDERVOLTAGE_CUTOFF,
    UART_BMS_REGISTER_DISCHARGE_OVER_CURRENT_CUTOFF,
    UART_BMS_REGISTER_CHARGE_OVER_CURRENT_CUTOFF,
    UART_BMS_REGISTER_OVERHEAT_CUTOFF,
    UART_BMS_REGISTER_LOW_TEMP_CHARGE_CUTOFF,
    UART_BMS_REGISTER_HARDWARE_VERSION,
    UART_BMS_REGISTER_PUBLIC_FIRMWARE_FLAGS,
    UART_BMS_REGISTER_INTERNAL_FIRMWARE_VERSION,
    UART_BMS_REGISTER_COUNT,
} uart_bms_register_id_t;

/**
 * @brief Supported raw value representations for TinyBMS registers.
 */
typedef enum {
    UART_BMS_VALUE_UINT16,
    UART_BMS_VALUE_INT16,
    UART_BMS_VALUE_UINT32,
    UART_BMS_VALUE_FLOAT32,
    UART_BMS_VALUE_INT8_PAIR,
} uart_bms_value_type_t;

/**
 * @brief Logical live-data fields updated from TinyBMS telemetry.
 */
typedef enum {
    UART_BMS_FIELD_NONE = 0,
    UART_BMS_FIELD_PACK_VOLTAGE,
    UART_BMS_FIELD_PACK_CURRENT,
    UART_BMS_FIELD_MIN_CELL_MV,
    UART_BMS_FIELD_MAX_CELL_MV,
    UART_BMS_FIELD_AVERAGE_TEMPERATURE,
    UART_BMS_FIELD_AUXILIARY_TEMPERATURE,
    UART_BMS_FIELD_STATE_OF_HEALTH,
    UART_BMS_FIELD_STATE_OF_CHARGE,
    UART_BMS_FIELD_MOS_TEMPERATURE,
    UART_BMS_FIELD_SYSTEM_STATUS,
    UART_BMS_FIELD_NEED_BALANCING,
    UART_BMS_FIELD_BALANCING_BITS,
    UART_BMS_FIELD_MAX_DISCHARGE_CURRENT,
    UART_BMS_FIELD_MAX_CHARGE_CURRENT,
    UART_BMS_FIELD_PACK_TEMPERATURE_MIN,
    UART_BMS_FIELD_PACK_TEMPERATURE_MAX,
    UART_BMS_FIELD_PEAK_DISCHARGE_CURRENT_LIMIT,
    UART_BMS_FIELD_BATTERY_CAPACITY,
    UART_BMS_FIELD_SERIES_CELL_COUNT,
    UART_BMS_FIELD_OVERVOLTAGE_CUTOFF,
    UART_BMS_FIELD_UNDERVOLTAGE_CUTOFF,
    UART_BMS_FIELD_DISCHARGE_OVER_CURRENT_LIMIT,
    UART_BMS_FIELD_CHARGE_OVER_CURRENT_LIMIT,
    UART_BMS_FIELD_OVERHEAT_CUTOFF,
    UART_BMS_FIELD_LOW_TEMP_CHARGE_CUTOFF,
    UART_BMS_FIELD_HARDWARE_VERSION,
    UART_BMS_FIELD_HARDWARE_CHANGES_VERSION,
    UART_BMS_FIELD_FIRMWARE_VERSION,
    UART_BMS_FIELD_FIRMWARE_FLAGS,
    UART_BMS_FIELD_INTERNAL_FIRMWARE_VERSION,
    UART_BMS_FIELD_UPTIME_SECONDS,
    UART_BMS_FIELD_ESTIMATED_TIME_LEFT,
} uart_bms_field_t;

/**
 * @brief Metadata describing a TinyBMS register.
 */
typedef struct {
    uart_bms_register_id_t id;   /**< Logical register identifier */
    uint16_t address;            /**< Base register address */
    uint8_t word_count;          /**< Number of consecutive 16-bit words */
    uart_bms_value_type_t type;  /**< Raw encoding used by the register */
    float scale;                 /**< Multiplicative scale applied to raw values */
    uart_bms_field_t primary_field;   /**< Primary live-data field updated */
    uart_bms_field_t secondary_field; /**< Secondary field (if applicable) */
    const char *name;            /**< Human readable name (from documentation) */
    const char *unit;            /**< Engineering unit string */
    const char *comment;         /**< Additional context or documentation */
} uart_bms_register_metadata_t;

extern const uart_bms_register_metadata_t g_uart_bms_registers[UART_BMS_REGISTER_COUNT];
extern const size_t g_uart_bms_register_count;
extern const uint16_t g_uart_bms_poll_addresses[UART_BMS_REGISTER_WORD_COUNT];

const uart_bms_register_metadata_t *uart_bms_protocol_find_by_address(uint16_t address);

#ifdef __cplusplus
}
#endif

