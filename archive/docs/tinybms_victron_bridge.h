/**
 * @file tinybms_victron_bridge.h
 * @brief Facade header for TinyBMS ↔ Victron bridge (split by tasks/modules)
 * @version 2.5.0
 */
#pragma once

#include <Arduino.h>
#include "shared_data.h"
#include "bridge_event_sink.h"
#include "cvl_types.h"
#include "hal/interfaces/ihal_uart.h"
#include "optimization/adaptive_polling.h"
#include "optimization/ring_buffer.h"

class HardwareSerial;
class WatchdogManager;
class ConfigManager;
class Logger;
class EventBus;

struct TinyBMS_Config {
    uint16_t fully_charged_voltage_mv = 0;
    uint16_t fully_discharged_voltage_mv = 0;
    uint16_t charge_finished_current_ma = 0;
    float    battery_capacity_ah = 0.0f;
    uint8_t  cell_count = 0;
    uint16_t overvoltage_cutoff_mv = 0;
    uint16_t undervoltage_cutoff_mv = 0;
    uint16_t discharge_overcurrent_a = 0;
    uint16_t charge_overcurrent_a = 0;
    float    overheat_cutoff_c = 0.0f;
    float    low_temp_charge_cutoff_c = 0.0f;
};

struct BridgeStats {
    uint32_t can_tx_count = 0;
    uint32_t can_rx_count = 0;
    uint32_t can_tx_errors = 0;
    uint32_t can_rx_errors = 0;
    uint32_t can_bus_off_count = 0;
    uint32_t can_queue_overflows = 0;
    uint32_t uart_errors = 0;
    uint32_t uart_success_count = 0;
    uint32_t uart_timeouts = 0;
    uint32_t uart_crc_errors = 0;
    uint32_t uart_retry_count = 0;
    uint32_t uart_latency_last_ms = 0;
    uint32_t uart_latency_max_ms = 0;
    float    uart_latency_avg_ms = 0.0f;
    uint32_t uart_poll_interval_current_ms = 100;
    float    cvl_current_v = 0.0f;
    float    ccl_limit_a = 0.0f;
    float    dcl_limit_a = 0.0f;
    double   energy_charged_wh = 0.0;
    double   energy_discharged_wh = 0.0;
    CVLState cvl_state = CVL_BULK;
    bool     victron_keepalive_ok = false;
    bool     cell_protection_active = false;
    uint32_t websocket_sent_count = 0;
    uint32_t websocket_dropped_count = 0;
};

namespace mqtt {
class Publisher;
} // namespace mqtt

class TinyBMS_Victron_Bridge {
public:
    TinyBMS_Victron_Bridge();

    bool begin();

    void setMqttPublisher(mqtt::Publisher* publisher);
    void setEventSink(BridgeEventSink* sink);
    void setUart(hal::IHalUart* uart);

    BridgeEventSink& eventSink() const;

    static void uartTask(void *pvParameters);
    static void canTask(void *pvParameters);
    static void cvlTask(void *pvParameters);

    bool readTinyRegisters(uint16_t start_addr, uint16_t count, uint16_t* output);

    bool sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc);

    void buildPGN_0x351(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x355(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x356(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x35A(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x35E(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x35F(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x371(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x378(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x379(const TinyBMS_LiveData& live, uint8_t* d);
    void buildPGN_0x382(const TinyBMS_LiveData& live, uint8_t* d);

    void keepAliveSend();
    void keepAliveProcessRX(uint32_t now);

    TinyBMS_Config   getConfig() const;

public:
    hal::IHalUart* tiny_uart_;
    optimization::AdaptivePoller uart_poller_;
    optimization::ByteRingBuffer uart_rx_buffer_;

    TinyBMS_Config   config_{};
    BridgeStats      stats{};

    mqtt::Publisher* mqtt_publisher_ = nullptr;
    BridgeEventSink* event_sink_ = nullptr;

    bool initialized_ = false;
    bool victron_keepalive_ok_ = false;

    uint32_t last_uart_poll_ms_   = 0;
    uint32_t last_pgn_update_ms_  = 0;
    uint32_t last_cvl_update_ms_  = 0;
    uint32_t last_keepalive_tx_ms_= 0;
    uint32_t last_keepalive_rx_ms_= 0;

    uint32_t uart_poll_interval_ms_  = 100;
    uint32_t pgn_update_interval_ms_ = 1000;
    uint32_t cvl_update_interval_ms_ = 20000;
    uint32_t keepalive_interval_ms_  = 1000;
    uint32_t keepalive_timeout_ms_   = 10000;

private:
    void updateEnergyCounters(uint32_t now_ms, const TinyBMS_LiveData& live);

    uint32_t last_energy_update_ms_ = 0;
};
