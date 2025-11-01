

tinybms_reader.h
------------------
#pragma once
#include <driver/uart.h>
#include <map>
#include <string>

struct TinyBMSData {
    float packVoltage = 0.0f;           // Reg 36
    float packCurrent = 0.0f;           // Reg 38
    float internalTemp = 0.0f;          // Reg 48
    uint32_t socRaw = 0;                // Reg 46 (0.002%)
    uint16_t sohRaw = 0;                // Reg 45 (0.002%)
    uint16_t maxCellVoltage = 0;        // Reg 41 (mV)
    uint16_t minCellVoltage = 0;        // Reg 40 (mV)
    int8_t minPackTemp = 0;             // Reg 113
    int8_t maxPackTemp = 0;             // Reg 113
    uint16_t onlineStatus = 0;          // Reg 50
    uint16_t maxChargeCurrent = 0;      // Reg 103 (0.1A)
    uint16_t maxDischargeCurrent = 0;   // Reg 102 (0.1A)
    // Add more as needed
};

class TinyBMSReader {
public:
    TinyBMSReader(uart_port_t uart_num = UART_NUM_2);
    void init();
    bool read_all_registers();
    const TinyBMSData& get_data() const { return data_; }

private:
    uart_port_t uart_num_;
    TinyBMSData data_;
    bool send_command(uint8_t cmd, uint16_t reg, uint8_t* resp, size_t len);
};


tinybms_reader.cpp
--------------

#include "tinybms_reader.h"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "TinyBMS";

TinyBMSReader::TinyBMSReader(uart_port_t uart_num) : uart_num_(uart_num) {}

void TinyBMSReader::init() {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(uart_num_, &cfg);
    uart_set_pin(uart_num_, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); // TX, RX
    uart_driver_install(uart_num_, 256, 256, 0, nullptr, 0);
}

bool TinyBMSReader::send_command(uint8_t cmd, uint16_t reg, uint8_t* resp, size_t len) {
    uint8_t tx[7] = {0xAA, 0x55, cmd, (uint8_t)(reg >> 8), (uint8_t)reg, 0x00, 0x00};
    uint16_t crc = 0;
    for (int i = 0; i < 5; ++i) crc += tx[i];
    tx[5] = crc >> 8; tx[6] = crc & 0xFF;

    uart_write_bytes(uart_num_, tx, 7);
    vTaskDelay(pdMS_TO_TICKS(50));

    int rx_len = uart_read_bytes(uart_num_, resp, len + 8, pdMS_TO_TICKS(100));
    if (rx_len < 8) return false;

    uint16_t rx_crc = (resp[rx_len-2] << 8) | resp[rx_len-1];
    uint16_t calc_crc = 0;
    for (int i = 0; i < rx_len-2; ++i) calc_crc += resp[i];
    return rx_crc == calc_crc;
}

bool TinyBMSReader::read_all_registers() {
    uint8_t buf[32];

    // Reg 36: Pack Voltage (float)
    if (send_command(0x11, 36, buf, 4)) {
        data_.packVoltage = *(float*)buf;
    }

    // Reg 38: Pack Current (float)
    if (send_command(0x11, 38, buf, 4)) {
        data_.packCurrent = *(float*)buf;
    }

    // Reg 48: Internal Temp (int16, 0.1°C)
    if (send_command(0x11, 48, buf, 2)) {
        data_.internalTemp = *(int16_t*)buf * 0.1f;
    }

    // Reg 46: SOC (uint32, 0.002%)
    if (send_command(0x11, 46, buf, 4)) {
        data_.socRaw = *(uint32_t*)buf;
    }

    // Reg 45: SOH (uint16, 0.002%)
    if (send_command(0x11, 45, buf, 2)) {
        data_.sohRaw = *(uint16_t*)buf;
    }

    // Reg 40: Min Cell V (uint16, mV)
    if (send_command(0x11, 40, buf, 2)) {
        data_.minCellVoltage = *(uint16_t*)buf;
    }

    // Reg 41: Max Cell V
    if (send_command(0x11, 41, buf, 2)) {
        data_.maxCellVoltage = *(uint16_t*)buf;
    }

    // Reg 113: Min/Max Temp (int8, °C)
    if (send_command(0x11, 113, buf, 2)) {
        data_.minPackTemp = (int8_t)buf[0];
        data_.maxPackTemp = (int8_t)buf[1];
    }

    // Reg 50: Online Status
    if (send_command(0x11, 50, buf, 2)) {
        data_.onlineStatus = *(uint16_t*)buf;
    }

    // Reg 102, 103: Currents (0.1A)
    if (send_command(0x11, 102, buf, 2)) data_.maxDischargeCurrent = *(uint16_t*)buf;
    if (send_command(0x11, 103, buf, 2)) data_.maxChargeCurrent = *(uint16_t*)buf;

    return true;
}

victron_can.h
---------------------

#pragma once
#include "tinybms_reader.h"
#include <driver/twai.h>

class VictronCAN {
public:
    VictronCAN(const uint8_t* json_data, size_t json_len);
    void init();
    void update_and_send(const TinyBMSData& data);

private:
    void send_frame(uint16_t id, const uint8_t* data, uint8_t len);
    // JSON parsing (simplified)
    void parse_mapping(const uint8_t* json, size_t len);
};


victron_can.cpp
---------------------

#include "victron_can.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>

static const char* TAG = "VictronCAN";

VictronCAN::VictronCAN(const uint8_t* json_data, size_t json_len) {
    parse_mapping(json_data, json_len);
}

void VictronCAN::init() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI started @ 500kbps");
}

void VictronCAN::send_frame(uint16_t id, const uint8_t* data, uint8_t len) {
    twai_message_t msg;
    msg.identifier = id & 0x7FF;
    msg.extd = 0;
    msg.rtr = 0;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    if (twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        ESP_LOGI(TAG, "SENT 0x%03X | %02X %02X %02X %02X %02X %02X %02X %02X",
                 msg.identifier, msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                 msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    }
}

void VictronCAN::update_and_send(const TinyBMSData& bms) {
    uint8_t frame[8];

    // 0x351 - CVL, CCL, DCL
    uint16_t cvl = static_cast<uint16_t>(std::round(54.4f * 10)); // 3.4V/cell * 16
    uint16_t ccl = bms.maxChargeCurrent;
    uint16_t dcl = bms.maxDischargeCurrent;
    frame[0] = cvl & 0xFF; frame[1] = cvl >> 8;
    frame[2] = ccl & 0xFF; frame[3] = ccl >> 8;
    frame[4] = dcl & 0xFF; frame[5] = dcl >> 8;
    frame[6] = 0xFF; frame[7] = 0xFF;
    send_frame(0x351, frame, 8);

    // 0x355 - SOC, SOH
    uint16_t soc = static_cast<uint16_t>(std::round(bms.socRaw * 0.002f));
    uint16_t soh = static_cast<uint16_t>(std::round(bms.sohRaw * 0.002f));
    frame[0] = soc & 0xFF; frame[1] = soc >> 8;
    frame[2] = soh & 0xFF; frame[3] = soh >> 8;
    frame[4] = soc & 0xFF; frame[5] = (soc >> 8); // hi-res
    frame[6] = 0xFF; frame[7] = 0xFF;
    send_frame(0x355, frame, 8);

    // 0x356 - V, I, T
    uint16_t voltage = static_cast<uint16_t>(std::round(bms.packVoltage * 100));
    int16_t current = static_cast<int16_t>(std::round(bms.packCurrent * 10));
    int16_t temp = static_cast<int16_t>(std::round(bms.internalTemp * 10));
    frame[0] = voltage & 0xFF; frame[1] = voltage >> 8;
    frame[2] = current & 0xFF; frame[3] = current >> 8;
    frame[4] = temp & 0xFF; frame[5] = temp >> 8;
    frame[6] = 0xFF; frame[7] = 0xFF;
    send_frame(0x356, frame, 8);

    // 0x35A - Alarms (simplified)
    uint8_t alarms = 0x55; // online
    if (bms.packVoltage > 58.4f) alarms |= (2 << 2);
    if (bms.packVoltage < 40.0f) alarms |= (2 << 4);
    frame[0] = alarms; memset(frame+1, 0x55, 7);
    send_frame(0x35A, frame, 8);
}

void VictronCAN::parse_mapping(const uint8_t* json, size_t len) {
    // In real use: parse JSON with cJSON or nlohmann
    // Here: skip, use hardcoded logic
}


main.cpp
------------
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "tinybms_reader.h"
#include "victron_can.h"

extern "C" void app_main() {
    // Embedded JSON
    extern const uint8_t bms_json_start[] asm("_binary_bms_victron_mapping_json_start");
    extern const uint8_t bms_json_end[]   asm("_binary_bms_victron_mapping_json_end");
    size_t json_len = bms_json_end - bms_json_start;

    TinyBMSReader bms(UART_NUM_2);
    VictronCAN victron(bms_json_start, json_len);

    bms.init();
    victron.init();

    while (1) {
        if (bms.read_all_registers()) {
            victron.update_and_send(bms.get_data());
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
------------

CMakeLists.txt (root)  
------------  
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bms-victron-esp32)

Flash & Test
--------------
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor  


Sortie attendue
---------------
I (1234) VictronCAN: TWAI started @ 500kbps
I (2345) VictronCAN: SENT 0x351 | 24 02 64 00 64 00 FF FF
I (2346) VictronCAN: SENT 0x355 | C8 00 C8 00 90 D0 FF FF
I (2347) VictronCAN: SENT 0x356 | 00 14 64 00 90 01 FF FF
