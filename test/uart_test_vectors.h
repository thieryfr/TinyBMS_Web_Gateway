#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint16_t kUartTestRegisterCount;
extern const uint16_t kUartTestSampleValues[];

size_t build_uart_test_frame(uint8_t *frame, size_t frame_size);
