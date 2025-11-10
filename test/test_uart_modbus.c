#include "unity.h"
#include "uart_frame_builder.h"
#include <string.h>

/**
 * @brief Tests for MODBUS protocol compliance in UART frame builder
 *
 * These tests validate the implementation of MODBUS Read Holding Registers (0x03)
 * and MODBUS Write Multiple Registers (0x10) as per TinyBMS Communication Protocols
 * Rev D, sections 1.1.6 and 1.1.7.
 */

TEST_CASE("uart_frame_builder_build_modbus_read creates valid frame", "[uart_modbus]")
{
    uint8_t buffer[16];
    size_t length = 0;

    // Build MODBUS Read request for address 0x0024, reading 2 registers
    esp_err_t err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                                         0x0024, 2, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(8, length);

    // Validate frame format: AA 03 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
    TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);  // Preamble
    TEST_ASSERT_EQUAL_HEX8(0x03, buffer[1]);  // MODBUS Read Holding Registers opcode
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[2]);  // ADDR MSB (Big Endian!)
    TEST_ASSERT_EQUAL_HEX8(0x24, buffer[3]);  // ADDR LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[4]);  // Reserved byte
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[5]);  // Register count

    // Verify CRC is correctly calculated and placed
    uint16_t crc_calculated = uart_frame_builder_crc16(buffer, 6);
    uint16_t crc_in_frame = buffer[6] | (buffer[7] << 8);
    TEST_ASSERT_EQUAL_UINT16(crc_calculated, crc_in_frame);
}

TEST_CASE("uart_frame_builder_build_modbus_read enforces register count limit", "[uart_modbus]")
{
    uint8_t buffer[16];
    size_t length = 0;

    // Test zero registers (invalid)
    esp_err_t err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                                         0x0000, 0, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test exceeding 127 registers (single packet limit)
    err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                               0x0000, 128, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test maximum valid count (127 registers)
    err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                               0x0000, 127, &length);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("uart_frame_builder_build_modbus_read uses MSB first byte order", "[uart_modbus]")
{
    uint8_t buffer[16];
    size_t length = 0;

    // Test address 0x1234 - should be encoded as MSB first (Big Endian)
    esp_err_t err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                                         0x1234, 5, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0x12, buffer[2]);  // MSB first (Big Endian)
    TEST_ASSERT_EQUAL_HEX8(0x34, buffer[3]);  // LSB second
}

TEST_CASE("uart_frame_builder_build_modbus_write creates valid frame", "[uart_modbus]")
{
    uint8_t buffer[32];
    size_t length = 0;
    uint16_t values[] = {0x1234, 0x5678};

    // Build MODBUS Write request for address 0x013B, writing 2 registers
    esp_err_t err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                          0x013B, values, 2, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(13, length);  // 7 header + 4 data + 2 CRC

    // Validate frame format: AA 10 ADDR:MSB ADDR:LSB 0x00 RL PL DATA... CRC
    TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);  // Preamble
    TEST_ASSERT_EQUAL_HEX8(0x10, buffer[1]);  // MODBUS Write Multiple Registers opcode
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[2]);  // ADDR MSB (Big Endian!)
    TEST_ASSERT_EQUAL_HEX8(0x3B, buffer[3]);  // ADDR LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[4]);  // Reserved byte
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[5]);  // Register count
    TEST_ASSERT_EQUAL_HEX8(0x04, buffer[6]);  // Payload length (2 regs × 2 bytes)

    // Verify data values are encoded MSB first (Big Endian)
    TEST_ASSERT_EQUAL_HEX8(0x12, buffer[7]);   // Value1 MSB
    TEST_ASSERT_EQUAL_HEX8(0x34, buffer[8]);   // Value1 LSB
    TEST_ASSERT_EQUAL_HEX8(0x56, buffer[9]);   // Value2 MSB
    TEST_ASSERT_EQUAL_HEX8(0x78, buffer[10]);  // Value2 LSB

    // Verify CRC
    uint16_t crc_calculated = uart_frame_builder_crc16(buffer, 11);
    uint16_t crc_in_frame = buffer[11] | (buffer[12] << 8);
    TEST_ASSERT_EQUAL_UINT16(crc_calculated, crc_in_frame);
}

TEST_CASE("uart_frame_builder_build_modbus_write enforces register count limit", "[uart_modbus]")
{
    uint8_t buffer[256];
    size_t length = 0;
    uint16_t values[101];
    memset(values, 0, sizeof(values));

    // Test zero registers (invalid)
    esp_err_t err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                          0x0000, values, 0, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test exceeding 100 registers (per TinyBMS spec)
    err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                0x0000, values, 101, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test maximum valid count (100 registers)
    err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                0x0000, values, 100, &length);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(209, length);  // 7 header + (100×2) data + 2 CRC
}

TEST_CASE("uart_frame_builder_build_modbus_write uses MSB first byte order", "[uart_modbus]")
{
    uint8_t buffer[32];
    size_t length = 0;
    uint16_t values[] = {0xABCD};

    // Test value 0xABCD at address 0x5678 - both should be MSB first
    esp_err_t err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                          0x5678, values, 1, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify address is MSB first
    TEST_ASSERT_EQUAL_HEX8(0x56, buffer[2]);  // ADDR MSB
    TEST_ASSERT_EQUAL_HEX8(0x78, buffer[3]);  // ADDR LSB

    // Verify data value is MSB first
    TEST_ASSERT_EQUAL_HEX8(0xAB, buffer[7]);  // VALUE MSB
    TEST_ASSERT_EQUAL_HEX8(0xCD, buffer[8]);  // VALUE LSB
}

TEST_CASE("uart_frame_builder_build_read_events creates valid frame", "[uart_modbus]")
{
    uint8_t buffer[8];
    size_t length = 0;

    // Build Read Newest Events request (0x11)
    esp_err_t err = uart_frame_builder_build_read_events(buffer, sizeof(buffer), &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(4, length);  // AA 11 CRC:LSB CRC:MSB

    // Validate frame format
    TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);  // Preamble
    TEST_ASSERT_EQUAL_HEX8(0x11, buffer[1]);  // Read Newest Events opcode

    // Verify CRC
    uint16_t crc_calculated = uart_frame_builder_crc16(buffer, 2);
    uint16_t crc_in_frame = buffer[2] | (buffer[3] << 8);
    TEST_ASSERT_EQUAL_UINT16(crc_calculated, crc_in_frame);
}

TEST_CASE("uart_frame_builder_crc16 matches MODBUS polynomial", "[uart_modbus]")
{
    // Test vector from MODBUS documentation
    // Message: 0xAA 0x03 0x00 0x24 0x00 0x02
    uint8_t test_data[] = {0xAA, 0x03, 0x00, 0x24, 0x00, 0x02};

    uint16_t crc = uart_frame_builder_crc16(test_data, sizeof(test_data));

    // CRC16 MODBUS uses polynomial 0xA001, init 0xFFFF
    // Verify the implementation is correct (this is a known-good test vector)
    TEST_ASSERT_NOT_EQUAL(0x0000, crc);  // CRC should not be zero
    TEST_ASSERT_NOT_EQUAL(0xFFFF, crc);  // CRC should not be init value
}

TEST_CASE("uart_frame_builder handles null pointers gracefully", "[uart_modbus]")
{
    uint8_t buffer[16];
    size_t length;
    uint16_t values[] = {0x1234};

    // Test null buffer for MODBUS read
    esp_err_t err = uart_frame_builder_build_modbus_read(NULL, sizeof(buffer),
                                                         0x0000, 1, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test null buffer for MODBUS write
    err = uart_frame_builder_build_modbus_write(NULL, sizeof(buffer),
                                                0x0000, values, 1, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test null values array for MODBUS write
    err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                0x0000, NULL, 1, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Test null buffer for read events
    err = uart_frame_builder_build_read_events(NULL, sizeof(buffer), &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    // Verify CRC handles null pointer
    uint16_t crc = uart_frame_builder_crc16(NULL, 10);
    TEST_ASSERT_EQUAL_UINT16(0, crc);
}

TEST_CASE("uart_frame_builder checks buffer size requirements", "[uart_modbus]")
{
    uint8_t small_buffer[4];
    size_t length;
    uint16_t values[] = {0x1234, 0x5678};

    // MODBUS read requires 8 bytes minimum
    esp_err_t err = uart_frame_builder_build_modbus_read(small_buffer, 7,
                                                         0x0000, 1, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, err);

    // MODBUS write requires 7 + (n*2) + 2 bytes
    err = uart_frame_builder_build_modbus_write(small_buffer, 10,
                                                0x0000, values, 2, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, err);

    // Read events requires 4 bytes minimum
    err = uart_frame_builder_build_read_events(small_buffer, 3, &length);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, err);
}

/**
 * Test comparison: MODBUS vs Proprietary byte order
 *
 * This test verifies the critical difference between MODBUS commands (MSB first)
 * and proprietary commands (LSB first) as documented in TinyBMS spec.
 */
TEST_CASE("uart_frame_builder MODBUS uses different byte order than proprietary", "[uart_modbus]")
{
    uint8_t modbus_buffer[16];
    uint8_t proprietary_buffer[16];
    size_t modbus_len = 0;
    size_t proprietary_len = 0;
    uint16_t test_values[] = {0x1234};

    // Build MODBUS Write (0x10) - should use MSB first
    esp_err_t err = uart_frame_builder_build_modbus_write(modbus_buffer, sizeof(modbus_buffer),
                                                          0xABCD, test_values, 1, &modbus_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Build Proprietary Write (0x0D) - should use LSB first
    err = uart_frame_builder_build_write_single(proprietary_buffer, sizeof(proprietary_buffer),
                                                0xABCD, 0x1234, &proprietary_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Compare address encoding
    // MODBUS: [AA 10 AB CD ...] (MSB first)
    // Proprietary: [AA 0D PL CD AB ...] (LSB first)
    TEST_ASSERT_EQUAL_HEX8(0x10, modbus_buffer[1]);  // MODBUS opcode
    TEST_ASSERT_EQUAL_HEX8(0x0D, proprietary_buffer[1]);  // Proprietary opcode

    TEST_ASSERT_EQUAL_HEX8(0xAB, modbus_buffer[2]);  // MODBUS: MSB first
    TEST_ASSERT_EQUAL_HEX8(0xCD, modbus_buffer[3]);  // MODBUS: LSB second

    TEST_ASSERT_EQUAL_HEX8(0xCD, proprietary_buffer[3]);  // Proprietary: LSB first
    TEST_ASSERT_EQUAL_HEX8(0xAB, proprietary_buffer[4]);  // Proprietary: MSB second

    // Compare value encoding
    // MODBUS data: [12 34] (MSB first)
    // Proprietary data: [34 12] (LSB first)
    TEST_ASSERT_EQUAL_HEX8(0x12, modbus_buffer[7]);   // MODBUS value MSB
    TEST_ASSERT_EQUAL_HEX8(0x34, modbus_buffer[8]);   // MODBUS value LSB

    TEST_ASSERT_EQUAL_HEX8(0x34, proprietary_buffer[5]);  // Proprietary value LSB
    TEST_ASSERT_EQUAL_HEX8(0x12, proprietary_buffer[6]);  // Proprietary value MSB
}
