#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_BMS_MAX_REGISTERS 39

typedef struct {
    uint16_t address;
    uint16_t raw_value;
} uart_bms_register_entry_t;

typedef struct {
    uint64_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    uint16_t min_cell_mv;
    uint16_t max_cell_mv;
    float state_of_charge_pct;
    float state_of_health_pct;
    float average_temperature_c;
    float mosfet_temperature_c;
    uint16_t balancing_bits;
    uint16_t alarm_bits;
    uint16_t warning_bits;
    uint32_t uptime_seconds;
    uint32_t cycle_count;
    size_t register_count;
    uart_bms_register_entry_t registers[UART_BMS_MAX_REGISTERS];
} uart_bms_live_data_t;

typedef void (*uart_bms_data_callback_t)(const uart_bms_live_data_t *data, void *context);

void uart_bms_init(void);
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);

esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context);
void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context);

esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length);
esp_err_t uart_bms_decode_frame(const uint8_t *frame, size_t length, uart_bms_live_data_t *out_data);

#ifdef __cplusplus
}
#endif
