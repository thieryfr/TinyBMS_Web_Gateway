#include "uart_frame_builder.h"

#include "uart_bms_protocol.h"

namespace {
constexpr uint8_t kTinyBmsPreamble = 0xAA;
constexpr uint8_t kTinyBmsOpcodeReadIndividual = 0x09;
constexpr uint8_t kTinyBmsOpcodeWriteIndividual = 0x0D;
constexpr uint8_t kTinyBmsOpcodeReadBlock = 0x07;
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

esp_err_t uart_frame_builder_build_write_single(uint8_t *buffer,
                                                 size_t buffer_size,
                                                 uint16_t address,
                                                 uint16_t value,
                                                 size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t payload_length = 4;  // address + value
    const size_t required = kFrameHeaderSize + payload_length + kCrcSize;
    if (buffer_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = kTinyBmsPreamble;
    buffer[offset++] = kTinyBmsOpcodeWriteIndividual;
    buffer[offset++] = static_cast<uint8_t>(payload_length);
    buffer[offset++] = static_cast<uint8_t>(address & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);
    buffer[offset++] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

esp_err_t uart_frame_builder_build_read_register(uint8_t *buffer,
                                                 size_t buffer_size,
                                                 uint16_t address,
                                                 size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t required = 5 + kCrcSize;  // AA 07 RL ADDR_L ADDR_H + CRC
    if (buffer_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = kTinyBmsPreamble;
    buffer[offset++] = kTinyBmsOpcodeReadBlock;
    buffer[offset++] = 0x01;  // request a single 16-bit register
    buffer[offset++] = static_cast<uint8_t>(address & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

esp_err_t uart_frame_builder_build_modbus_read(uint8_t *buffer,
                                               size_t buffer_size,
                                               uint16_t start_address,
                                               uint8_t register_count,
                                               size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Frame format: AA 03 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
    constexpr size_t kModbusReadFrameSize = 8;
    if (buffer_size < kModbusReadFrameSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Limit to 127 registers (max for single packet, per TinyBMS spec section 1.1.6)
    if (register_count == 0 || register_count > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x03;  // MODBUS Read Holding Registers opcode
    buffer[offset++] = static_cast<uint8_t>((start_address >> 8) & 0xFF);  // ADDR MSB (Big Endian)
    buffer[offset++] = static_cast<uint8_t>(start_address & 0xFF);  // ADDR LSB
    buffer[offset++] = 0x00;  // Reserved byte
    buffer[offset++] = register_count;  // RL (number of registers to read)

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);  // CRC LSB
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);  // CRC MSB

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

esp_err_t uart_frame_builder_build_modbus_write(uint8_t *buffer,
                                                size_t buffer_size,
                                                uint16_t start_address,
                                                const uint16_t *values,
                                                uint8_t register_count,
                                                size_t *out_length)
{
    if (buffer == nullptr || values == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Limit to 100 registers per TinyBMS spec section 1.1.7
    if (register_count == 0 || register_count > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t payload_len = register_count * 2;
    const size_t required = 7 + payload_len + 2;  // header(7) + payload + CRC(2)
    if (buffer_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x10;  // MODBUS Write Multiple Registers opcode
    buffer[offset++] = static_cast<uint8_t>((start_address >> 8) & 0xFF);  // ADDR MSB (Big Endian)
    buffer[offset++] = static_cast<uint8_t>(start_address & 0xFF);  // ADDR LSB
    buffer[offset++] = 0x00;  // Reserved byte
    buffer[offset++] = register_count;  // RL (number of registers to write)
    buffer[offset++] = static_cast<uint8_t>(payload_len);  // PL (payload length in bytes)

    // Write register values in MSB first (Big Endian) format
    for (size_t i = 0; i < register_count; ++i) {
        buffer[offset++] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);  // MSB
        buffer[offset++] = static_cast<uint8_t>(values[i] & 0xFF);  // LSB
    }

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);  // CRC LSB
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);  // CRC MSB

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

esp_err_t uart_frame_builder_build_read_events(uint8_t *buffer,
                                               size_t buffer_size,
                                               size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Frame format: AA 11 CRC:LSB CRC:MSB
    constexpr size_t kEventsRequestSize = 4;
    if (buffer_size < kEventsRequestSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x11;  // Read Newest Events opcode

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);  // CRC LSB
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);  // CRC MSB

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}

}  // extern "C"

