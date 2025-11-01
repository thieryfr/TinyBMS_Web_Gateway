/**
 * @file bridge_keepalive.cpp
 * @brief KeepAlive (0x305) bidirectional: RX detect + TX heartbeat
 */
#include <Arduino.h>
#include "bridge_keepalive.h"
#include "logger.h"
#include "config_manager.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_can.h"
#include "event/event_types_v2.h"

#include <cstring>

using tinybms::events::AlarmCode;
using tinybms::events::AlarmRaised;
using tinybms::events::AlarmSeverity;
using tinybms::events::EventSource;
using tinybms::events::StatusLevel;
using tinybms::events::StatusMessage;

extern Logger logger;
extern ConfigManager config;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[KA] ") + (msg)); } while(0)

void TinyBMS_Victron_Bridge::keepAliveSend(){
    uint32_t now = millis();
    if (now - last_keepalive_tx_ms_ < keepalive_interval_ms_) return;
    uint8_t d[8] = {0};
    sendVictronPGN(VICTRON_PGN_KEEPALIVE, d, 1);
    last_keepalive_tx_ms_ = now;
}

void TinyBMS_Victron_Bridge::keepAliveProcessRX(uint32_t now_ms){
    hal::CanFrame f;
    while (hal::HalManager::instance().can().receive(f, 0) == hal::Status::Ok) {
        stats.can_rx_count++;
        if (!f.extended && f.id == VICTRON_PGN_KEEPALIVE) {
            last_keepalive_rx_ms_ = now_ms;
            if (!victron_keepalive_ok_) {
                victron_keepalive_ok_ = true;
                stats.victron_keepalive_ok = true;
                // Inform observers (WebSocket, REST) that the keep-alive is healthy again
                StatusMessage status{};
                status.metadata.source = EventSource::Can;
                status.level = StatusLevel::Info;
                std::strncpy(status.message, "VE.Can keepalive OK", sizeof(status.message) - 1);
                status.message[sizeof(status.message) - 1] = '\0';
                eventSink().publish(status);
                BRIDGE_LOG(LOG_INFO, "VE.Can keepalive detected");
            }
        }
    }

    if (victron_keepalive_ok_ && (now_ms - last_keepalive_rx_ms_ > keepalive_timeout_ms_)) {
        victron_keepalive_ok_ = false;
        stats.victron_keepalive_ok = false;
        AlarmRaised alarm{};
        alarm.metadata.source = EventSource::Can;
        alarm.alarm.alarm_code = static_cast<uint16_t>(AlarmCode::CanKeepAliveLost);
        alarm.alarm.severity = static_cast<uint8_t>(AlarmSeverity::Warning);
        std::strncpy(alarm.alarm.message, "VE.Can keepalive lost", sizeof(alarm.alarm.message) - 1);
        alarm.alarm.message[sizeof(alarm.alarm.message) - 1] = '\0';
        alarm.alarm.value = 0.0f;
        alarm.alarm.is_active = true;
        eventSink().publish(alarm);
        BRIDGE_LOG(LOG_WARN, "VE.Can keepalive TIMEOUT");
    }
}
