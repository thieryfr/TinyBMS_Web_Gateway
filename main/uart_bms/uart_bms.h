#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_bus.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of TinyBMS register values expected in a single frame. */
#define UART_BMS_MAX_REGISTERS 39

/** Event identifier emitted when a TinyBMS telemetry sample is available. */
#define UART_BMS_EVENT_ID_LIVE_DATA 0x2000u

/**
 * @brief Raw TinyBMS register sample captured from a frame.
 */
typedef struct {
    uint16_t address;   /**< Register address queried from TinyBMS. */
    uint16_t raw_value; /**< Raw 16-bit register content. */
} uart_bms_register_value_t;

/**
 * @brief Normalised TinyBMS telemetry shared with the rest of the application.
 */
typedef struct {
    uint64_t timestamp_ms;             /**< Host timestamp when the frame was parsed. */

    bool pack_voltage_valid;           /**< Whether pack_voltage_v contains valid data. */
    float pack_voltage_v;              /**< Pack voltage in volts. */

    bool pack_current_valid;           /**< Whether pack_current_a contains valid data. */
    float pack_current_a;              /**< Pack current in amperes. */

    bool min_cell_mv_valid;            /**< Whether min_cell_mv contains valid data. */
    uint16_t min_cell_mv;              /**< Minimum cell voltage in millivolts. */

    bool max_cell_mv_valid;            /**< Whether max_cell_mv contains valid data. */
    uint16_t max_cell_mv;              /**< Maximum cell voltage in millivolts. */

    bool state_of_charge_valid;        /**< Whether state_of_charge_pct is valid. */
    float state_of_charge_pct;         /**< State of charge expressed as a percentage. */

    bool state_of_health_valid;        /**< Whether state_of_health_pct is valid. */
    float state_of_health_pct;         /**< State of health expressed as a percentage. */

    bool average_temperature_valid;    /**< Whether average_temperature_c is valid. */
    float average_temperature_c;       /**< Average temperature in degrees Celsius. */

    bool mosfet_temperature_valid;     /**< Whether mosfet_temperature_c is valid. */
    float mosfet_temperature_c;        /**< MOSFET temperature in degrees Celsius. */

    bool balancing_bits_valid;         /**< Whether balancing_bits is valid. */
    uint16_t balancing_bits;           /**< TinyBMS balancing status bitmask. */

    bool alarm_bits_valid;             /**< Whether alarm_bits is valid. */
    uint16_t alarm_bits;               /**< TinyBMS alarm bitmask. */

    bool warning_bits_valid;           /**< Whether warning_bits is valid. */
    uint16_t warning_bits;             /**< TinyBMS warning bitmask. */

    bool uptime_valid;                 /**< Whether uptime_seconds is valid. */
    uint32_t uptime_seconds;           /**< TinyBMS uptime in seconds. */

    bool cycle_count_valid;            /**< Whether cycle_count is valid. */
    uint32_t cycle_count;              /**< Cumulative charge cycle counter. */

    size_t register_count;             /**< Number of register samples stored in registers. */
    uart_bms_register_value_t registers[UART_BMS_MAX_REGISTERS]; /**< Raw register snapshot. */
} uart_bms_live_data_t;

/** Callback invoked when a TinyBMS telemetry frame has been decoded. */
typedef void (*uart_bms_data_callback_t)(const uart_bms_live_data_t *data, void *context);

/**
 * @brief Runtime configuration applied when initialising the TinyBMS UART driver.
 */
typedef struct {
    int uart_port;            /**< UART peripheral index used to talk to TinyBMS. */
    int tx_pin;               /**< GPIO used for the UART TX signal. */
    int rx_pin;               /**< GPIO used for the UART RX signal. */
    int baud_rate;            /**< UART baud rate. */
    TickType_t poll_interval; /**< Polling interval (FreeRTOS ticks) between requests. */
} uart_bms_config_t;

/** Obtain the default TinyBMS UART configuration. */
void uart_bms_get_default_config(uart_bms_config_t *out_config);

/** Attach the application wide event publisher used for TinyBMS updates. */
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);

/** Apply a custom configuration that will be used on the next ::uart_bms_init call. */
void uart_bms_apply_config(const uart_bms_config_t *config);

/** Initialise the TinyBMS UART driver. */
void uart_bms_init(void);

/** Stop the TinyBMS UART driver and release allocated resources. */
void uart_bms_deinit(void);

/** Register a consumer that should receive decoded TinyBMS telemetry samples. */
esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context);

/** Unregister a previously registered TinyBMS telemetry listener. */
void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context);

/** Decode and publish the provided TinyBMS frame. */
esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length);

/** Decode the provided TinyBMS frame into ::uart_bms_live_data_t. */
esp_err_t uart_bms_decode_frame(const uint8_t *frame, size_t length, uart_bms_live_data_t *out_data);

#ifdef __cplusplus
}
#endif

