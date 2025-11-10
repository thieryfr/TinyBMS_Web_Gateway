#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the TinyBMS poll frame requesting all configured registers.
 *
 * @param buffer Destination buffer that receives the frame bytes.
 * @param buffer_size Capacity of @p buffer in bytes.
 * @param out_length Optional pointer updated with the resulting frame length.
 * @return ESP_OK on success, or an ESP_ERR_* code if the buffer is too small or
 *         an argument is invalid.
 */
esp_err_t uart_frame_builder_build_poll_request(uint8_t *buffer,
                                                size_t buffer_size,
                                                size_t *out_length);

esp_err_t uart_frame_builder_build_write_single(uint8_t *buffer,
                                                 size_t buffer_size,
                                                 uint16_t address,
                                                 uint16_t value,
                                                 size_t *out_length);

esp_err_t uart_frame_builder_build_read_register(uint8_t *buffer,
                                                 size_t buffer_size,
                                                 uint16_t address,
                                                 size_t *out_length);

/**
 * @brief Build a MODBUS Read Holding Registers request (0x03)
 *
 * IMPORTANT: MODBUS commands use MSB first (Big Endian) byte order,
 * unlike proprietary commands which use LSB first.
 *
 * Frame format: 0xAA 0x03 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
 *
 * @param buffer Destination buffer
 * @param buffer_size Buffer capacity (min 8 bytes)
 * @param start_address Starting register address (MODBUS format: MSB first)
 * @param register_count Number of registers to read (max 127)
 * @param out_length Optional pointer updated with resulting frame length
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t uart_frame_builder_build_modbus_read(uint8_t *buffer,
                                               size_t buffer_size,
                                               uint16_t start_address,
                                               uint8_t register_count,
                                               size_t *out_length);

/**
 * @brief Build a MODBUS Write Multiple Registers request (0x10)
 *
 * IMPORTANT: MODBUS commands use MSB first (Big Endian) byte order.
 *
 * Frame format: 0xAA 0x10 ADDR:MSB ADDR:LSB 0x00 RL PL DATA1:MSB DATA1:LSB ... CRC:LSB CRC:MSB
 *
 * @param buffer Destination buffer
 * @param buffer_size Buffer capacity
 * @param start_address Starting register address (MODBUS format: MSB first)
 * @param values Array of register values to write (will be encoded MSB first)
 * @param register_count Number of registers to write (max 100)
 * @param out_length Optional pointer updated with resulting frame length
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t uart_frame_builder_build_modbus_write(uint8_t *buffer,
                                                size_t buffer_size,
                                                uint16_t start_address,
                                                const uint16_t *values,
                                                uint8_t register_count,
                                                size_t *out_length);

/**
 * @brief Build Read Newest Events request (0x11)
 *
 * Frame format: 0xAA 0x11 CRC:LSB CRC:MSB
 *
 * Response will be multi-frame:
 * - First frame: BMS timestamp
 * - Following frames: Individual events with timestamp and ID
 *
 * @param buffer Destination buffer
 * @param buffer_size Buffer capacity (min 4 bytes)
 * @param out_length Optional pointer updated with resulting frame length
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t uart_frame_builder_build_read_events(uint8_t *buffer,
                                               size_t buffer_size,
                                               size_t *out_length);

/**
 * @brief Compute the TinyBMS CRC16 used for UART frames.
 *
 * @param data Pointer to the data buffer.
 * @param length Number of bytes in @p data.
 * @return 16-bit CRC value (polynomial 0xA001, initial value 0xFFFF).
 */
uint16_t uart_frame_builder_crc16(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

