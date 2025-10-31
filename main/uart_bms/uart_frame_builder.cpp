#include "uart_frame_builder.h"

#include "uart_bms_protocol.h"

namespace {
constexpr uint8_t kTinyBmsPreamble = 0xAA;
constexpr uint8_t kTinyBmsOpcodeReadIndividual = 0x09;
constexpr size_t kFrameHeaderSize = 3;  // preamble + opcode + payload length
constexpr size_t kCrcSize = 2;
}  // namespace

extern "C" {

uint16_t uart_frame_builder_crc16(const uint8_t *data, size_t length)
{
    if (data == nullptr) {
        return 0;
    }

    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

esp_err_t uart_frame_builder_build_poll_request(uint8_t *buffer,
                                                 size_t buffer_size,
                                                 size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t payload_length = UART_BMS_REGISTER_WORD_COUNT * sizeof(uint16_t);
    const size_t required = kFrameHeaderSize + payload_length + kCrcSize;
    if (buffer_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = kTinyBmsPreamble;
    buffer[offset++] = kTinyBmsOpcodeReadIndividual;
    buffer[offset++] = static_cast<uint8_t>(payload_length);

    for (size_t i = 0; i < UART_BMS_REGISTER_WORD_COUNT; ++i) {
        const uint16_t address = g_uart_bms_poll_addresses[i];
        buffer[offset++] = static_cast<uint8_t>(address & 0xFF);
        buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);
    }

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

}  // extern "C"

