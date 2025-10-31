#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "shared_data.h"
#include "uart_bms.h"
#include "uart_bms_protocol.h"

class UartResponseParser {
public:
    UartResponseParser();

    esp_err_t parseFrame(const uint8_t* frame,
                         size_t length,
                         uint64_t timestamp_ms,
                         uart_bms_live_data_t* legacy_out,
                         TinyBMS_LiveData* shared_out);

    void recordTimeout();
    void getDiagnostics(uart_bms_parser_diagnostics_t* out) const;

private:
    esp_err_t validateFrame(const uint8_t* frame,
                            size_t length,
                            size_t* register_count) const;

    void decodeRegisters(const uint8_t* frame,
                         size_t register_count,
                         uart_bms_live_data_t* legacy_out,
                         TinyBMS_LiveData* shared_out);

    void appendSnapshot(TinyBMS_LiveData& shared_out,
                        uint16_t address,
                        uart_bms_value_type_t value_type,
                        int32_t raw_value,
                        uint8_t word_count,
                        const uint16_t* word_ptr);

private:
    uart_bms_parser_diagnostics_t diagnostics_{};
};

