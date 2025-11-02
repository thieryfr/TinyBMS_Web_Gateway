#include "uart_test_vectors.h"

#include "uart_bms.h"

const uint16_t kUartTestRegisterCount = UART_BMS_REGISTER_WORD_COUNT;

const uint16_t kUartTestSampleValues[UART_BMS_REGISTER_WORD_COUNT] = {
    0x7D00, 0x7D64, 0x7DC8, 0x7E2C, 0x7E90, 0x7EF4, 0x7F58, 0x7FBC, 0x8020,
    0x8084, 0x80E8, 0x814C, 0x81B0, 0x8214, 0x8278, 0x82DC, 0x3456, 0x0012,
    0x6666, 0x424D, 0xCCCD, 0xC144, 0x0C80, 0x0CF8, 0x00F5, 0x012C, 0xB22F,
    0x2CC0, 0x0482, 0x0113, 0x0091, 0x0002, 0x0003, 0x1C12, 0x0078, 0x1C12,
    0x0078, 0x2F12, 0x0010, 0x1068, 0x0BB8, 0x0096, 0x003F, 0x003E, 0xFFF0,
    0x0102, 0x1234, 0x0456, 0x0000, 0x0000, 0x0000, 0x4E53, 0x3231, 0x3433,
    0x3635, 0x3837, 0x3039,
};

static uint16_t compute_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

size_t build_uart_test_frame(uint8_t *frame, size_t frame_size)
{
    if (frame == NULL || frame_size == 0U) {
        return 0;
    }

    size_t payload_len = kUartTestRegisterCount * sizeof(uint16_t);
    size_t total_len = payload_len + 5U;
    if (total_len > frame_size) {
        return 0;
    }

    frame[0] = 0xAA;
    frame[1] = 0x09;
    frame[2] = (uint8_t)payload_len;

    for (size_t i = 0; i < kUartTestRegisterCount; ++i) {
        frame[3 + i * 2] = (uint8_t)(kUartTestSampleValues[i] & 0xFFU);
        frame[4 + i * 2] = (uint8_t)(kUartTestSampleValues[i] >> 8);
    }

    uint16_t crc = compute_crc16(frame, total_len - 2U);
    frame[total_len - 2] = (uint8_t)(crc & 0xFFU);
    frame[total_len - 1] = (uint8_t)(crc >> 8);

    return total_len;
}
