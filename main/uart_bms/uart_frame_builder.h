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

