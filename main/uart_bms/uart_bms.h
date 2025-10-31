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

/** Maximum number of registers decoded from a TinyBMS frame. */
#define UART_BMS_MAX_REGISTERS 39

/** Event identifier used when publishing TinyBMS live data samples. */
#define UART_BMS_EVENT_ID_LIVE_DATA 0x2000u

/** Raw register snapshot captured from a TinyBMS response frame. */
typedef struct {
    uint16_t address;      /**< Register address that was sampled. */
    uint16_t raw_value;    /**< 16-bit value reported for the register. */
} uart_bms_register_value_t;

/** Normalised telemetry decoded from a TinyBMS frame. */
typedef struct {
    uint64_t timestamp_ms;           /**< Host timestamp associated with the frame. */
    float pack_voltage_v;            /**< Pack voltage in volts. */
    float pack_current_a;            /**< Pack current in amperes. */
    uint16_t min_cell_mv;            /**< Minimum cell voltage in millivolts. */
    uint16_t max_cell_mv;            /**< Maximum cell voltage in millivolts. */
    float state_of_charge_pct;       /**< State of charge expressed as a percentage. */
    float state_of_health_pct;       /**< State of health expressed as a percentage. */
    float average_temperature_c;     /**< Average temperature in degrees Celsius. */
    float mosfet_temperature_c;      /**< MOSFET temperature in degrees Celsius. */
    uint16_t balancing_bits;         /**< Bitmask describing balancing activity. */
    uint16_t alarm_bits;             /**< Bitmask with alarm flags. */
    uint16_t warning_bits;           /**< Bitmask with warning flags. */
    uint32_t uptime_seconds;         /**< Uptime reported by the BMS in seconds. */
    uint32_t cycle_count;            /**< Charge cycle counter. */
    size_t register_count;           /**< Number of register entries captured. */
    uart_bms_register_value_t registers[UART_BMS_MAX_REGISTERS]; /**< Raw register dump. */
} uart_bms_live_data_t;

/** Callback signature used to subscribe to TinyBMS live data updates. */
typedef void (*uart_bms_data_callback_t)(const uart_bms_live_data_t *data, void *context);

/** Runtime configuration for the UART TinyBMS driver. */
typedef struct {
    int uart_port;           /**< UART port number used to communicate with TinyBMS. */
    int tx_pin;              /**< GPIO number for the UART TX signal. */
    int rx_pin;              /**< GPIO number for the UART RX signal. */
    int baud_rate;           /**< Baud rate configured for the UART peripheral. */
    TickType_t poll_interval;/**< Polling interval used when requesting new frames. */
} uart_bms_config_t;

/** Attach the application wide event publisher to the TinyBMS driver. */
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Override the default TinyBMS driver configuration.
 *
 * The configuration is copied and used for subsequent ::uart_bms_init calls.
 * Passing NULL restores the built-in defaults.
 */
void uart_bms_apply_config(const uart_bms_config_t *config);

/** Initialise the TinyBMS UART driver and start the polling task. */
void uart_bms_init(void);

/** Stop the TinyBMS UART driver and release all allocated resources. */
void uart_bms_deinit(void);

/** Register a listener that will receive decoded TinyBMS frames. */
esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context);

/** Remove a listener previously registered with ::uart_bms_register_listener. */
void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context);

/** Decode and publish a TinyBMS frame that was received on the UART bus. */
esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length);

/** Parse a TinyBMS frame without publishing any events. */
esp_err_t uart_bms_decode_frame(const uint8_t *frame, size_t length, uart_bms_live_data_t *out_data);

#ifdef __cplusplus
}
#endif

