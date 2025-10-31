/**
 * @file bridge_can.cpp
 * @brief CAN TX (PGNs) + RX polling (KeepAlive)
 */
#include <Arduino.h>
#include <string.h>
#include <cstring>
#include <math.h>
#include <cmath>
#include <algorithm>
#include "bridge_can.h"
#include "bridge_keepalive.h"
#include "logger.h"
#include "config_manager.h"
#include "victron_can_mapping.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_can.h"
#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"
#include "victron_alarm_utils.h"

using tinybms::events::AlarmCode;
using tinybms::events::AlarmRaised;
using tinybms::events::AlarmSeverity;
using tinybms::events::EventSource;
using tinybms::events::LiveDataUpdate;

extern Logger logger;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t statsMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CAN] ") + (msg)); } while(0)

static inline void put_u16_le(uint8_t* b, uint16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline void put_s16_le(uint8_t* b, int16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline void put_u32_le(uint8_t* b, uint32_t v){
    b[0] = static_cast<uint8_t>(v & 0xFFu);
    b[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    b[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    b[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
static inline uint16_t clamp_u16(int v){ return (uint16_t) (v<0?0:(v>65535?65535:v)); }
static inline int16_t  clamp_s16(int v){ return (int16_t)  (v<-32768?-32768:(v>32767?32767:v)); }
static inline int      round_i(float x){ return (int)lrintf(x); }

namespace {

void publishCanAlarm(BridgeEventSink& sink,
                     AlarmCode code,
                     const char* message,
                     AlarmSeverity severity,
                     float value) {
    AlarmRaised event{};
    event.metadata.source = EventSource::Can;
    event.alarm.alarm_code = static_cast<uint16_t>(code);
    event.alarm.severity = static_cast<uint8_t>(severity);
    if (message) {
        std::strncpy(event.alarm.message, message, sizeof(event.alarm.message) - 1);
        event.alarm.message[sizeof(event.alarm.message) - 1] = '\0';
    }
    event.alarm.value = value;
    event.alarm.is_active = true;
    victron::annotateAlarm(code, severity, event.alarm);
    sink.publish(event);
}

String getRegisterString(const TinyBMS_LiveData& live, uint16_t address) {
    const TinyRegisterSnapshot* snap = live.findSnapshot(address);
    if (snap && snap->has_text && snap->text_value.length() > 0) {
        return snap->text_value;
    }
    return String();
}

uint8_t sanitize7bit(char c) {
    uint8_t uc = static_cast<uint8_t>(c);
    uc &= 0x7Fu;
    if (uc < 0x20u && uc != 0u) {
        uc = 0x20u;
    }
    return uc;
}

void copyAsciiPadded(uint8_t* dest, size_t length, const String& source, size_t start_index = 0) {
    for (size_t i = 0; i < length; ++i) {
        uint8_t value = 0;
        size_t idx = start_index + i;
        if (idx < static_cast<size_t>(source.length())) {
            value = sanitize7bit(source.charAt(static_cast<unsigned int>(idx)));
        }
        dest[i] = value;
    }
}

String resolveManufacturerName(const TinyBMS_LiveData& live) {
    String manufacturer = getRegisterString(live, 500);
    if (manufacturer.length() == 0) {
        manufacturer = "TinyBMS";
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            manufacturer = config.victron.manufacturer_name;
            xSemaphoreGive(configMutex);
        }
    }
    if (manufacturer.length() == 0) {
        manufacturer = "TinyBMS";
    }
    return manufacturer;
}

String resolveBatteryName(const TinyBMS_LiveData& live) {
    String name;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        name = config.victron.battery_name;
        xSemaphoreGive(configMutex);
    }
    if (name.length() == 0) {
        name = getRegisterString(live, 502);
    }
    if (name.length() == 0) {
        name = "Lithium Battery";
    }
    return name;
}

String resolveBatteryFamily(const TinyBMS_LiveData& live) {
    String family = getRegisterString(live, 502);
    if (family.length() == 0) {
        family = resolveBatteryName(live);
    }
    return family;
}

uint32_t encodeEnergyWh(double energy_wh) {
    if (!(energy_wh > 0.0)) {
        return 0;
    }
    double raw = energy_wh * 100.0;
    if (raw < 0.0) {
        raw = 0.0;
    }
    const double max_raw = 4294967295.0;
    if (raw > max_raw) {
        raw = max_raw;
    }
    return static_cast<uint32_t>(raw + 0.5);
}

struct VictronMappingContext {
    const TinyBMS_LiveData& live;
    const BridgeStats& stats;
    ConfigManager::VictronConfig::Thresholds thresholds{};
    bool thresholds_loaded = false;
    bool comm_error_cached = false;
    bool comm_error_value = false;
    bool derate_cached = false;
    bool derate_value = false;
};

bool ensureThresholds(VictronMappingContext& ctx) {
    if (ctx.thresholds_loaded) {
        return true;
    }
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ctx.thresholds = config.victron.thresholds;
        ctx.thresholds_loaded = true;
        xSemaphoreGive(configMutex);
        return true;
    }
    return false;
}

bool getLiveDataValue(TinyLiveDataField field, const TinyBMS_LiveData& live, float& value) {
    switch (field) {
        case TinyLiveDataField::Voltage:
            value = live.voltage;
            return true;
        case TinyLiveDataField::Current:
            value = live.current;
            return true;
        case TinyLiveDataField::SocPercent:
            value = live.soc_percent;
            return true;
        case TinyLiveDataField::SohPercent:
            value = live.soh_percent;
            return true;
        case TinyLiveDataField::Temperature:
            value = static_cast<float>(live.temperature);
            return true;
        case TinyLiveDataField::MinCellMv:
            value = static_cast<float>(live.min_cell_mv);
            return true;
        case TinyLiveDataField::MaxCellMv:
            value = static_cast<float>(live.max_cell_mv);
            return true;
        case TinyLiveDataField::BalancingBits:
            value = static_cast<float>(live.balancing_bits);
            return true;
        case TinyLiveDataField::MaxChargeCurrent:
            value = static_cast<float>(live.max_charge_current) / 10.0f;
            return true;
        case TinyLiveDataField::MaxDischargeCurrent:
            value = static_cast<float>(live.max_discharge_current) / 10.0f;
            return true;
        case TinyLiveDataField::OnlineStatus:
            value = static_cast<float>(live.online_status);
            return true;
        case TinyLiveDataField::CellImbalanceMv:
            value = static_cast<float>(live.cell_imbalance_mv);
            return true;
        case TinyLiveDataField::NeedBalancing:
        case TinyLiveDataField::None:
        default:
            break;
    }
    return false;
}

bool computeCommError(VictronMappingContext& ctx) {
    if (!ctx.comm_error_cached) {
        ctx.comm_error_value = (ctx.stats.uart_errors > 0 || ctx.stats.can_tx_errors > 0 || !ctx.stats.victron_keepalive_ok);
        ctx.comm_error_cached = true;
    }
    return ctx.comm_error_value;
}

bool computeDerate(VictronMappingContext& ctx) {
    if (!ctx.derate_cached) {
        ensureThresholds(ctx);
        const uint16_t minLimit = static_cast<uint16_t>(ctx.thresholds.derate_current_a * 10.0f);
        ctx.derate_value = (ctx.live.max_charge_current <= minLimit || ctx.live.max_discharge_current <= minLimit);
        ctx.derate_cached = true;
    }
    return ctx.derate_value;
}

bool computeFunctionValue(const VictronCanFieldDefinition& field,
                          const TinyBMS_Victron_Bridge& bridge,
                          VictronMappingContext& ctx,
                          float& value) {
    String id = field.source.identifier;
    id.toLowerCase();

    const auto& live = ctx.live;
    const auto& stats = ctx.stats;

    if (id == "cvl_dynamic") {
        float cvl = stats.cvl_current_v > 0.0f ? stats.cvl_current_v : live.voltage;
        value = cvl;
        return true;
    }
    if (id == "ccl_limit") {
        float ccl = stats.ccl_limit_a > 0.0f ? stats.ccl_limit_a : (static_cast<float>(live.max_charge_current) / 10.0f);
        value = ccl;
        return true;
    }
    if (id == "dcl_limit") {
        float dcl = stats.dcl_limit_a > 0.0f ? stats.dcl_limit_a : (static_cast<float>(live.max_discharge_current) / 10.0f);
        value = dcl;
        return true;
    }

    ensureThresholds(ctx);
    const auto& th = ctx.thresholds;
    const float voltage = live.voltage;
    const float temperature_c = static_cast<float>(live.temperature) / 10.0f;
    const uint16_t imbalance = live.cell_imbalance_mv;
    const bool commErr = computeCommError(ctx);
    const bool lowSOC = (live.soc_percent <= th.soc_low_percent);
    const bool highSOC = (live.soc_percent >= th.soc_high_percent);
    const bool derate = computeDerate(ctx);

    if (id == "alarm_undervoltage") {
        value = (voltage < th.undervoltage_v && voltage > 0.1f) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_overvoltage") {
        value = (voltage > th.overvoltage_v) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_overtemperature") {
        value = (temperature_c > th.overtemp_c) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_low_temp_charge") {
        value = (temperature_c < th.low_temp_charge_c && live.current > 3.0f) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_cell_imbalance") {
        value = (imbalance > th.imbalance_alarm_mv) ? 2.0f : ((imbalance > th.imbalance_warn_mv) ? 1.0f : 0.0f);
        return true;
    }
    if (id == "alarm_comms") {
        value = commErr ? 1.0f : 0.0f;
        return true;
    }
    if (id == "warn_low_soc") {
        value = lowSOC ? 1.0f : 0.0f;
        return true;
    }
    if (id == "warn_derate_high_soc") {
        value = (derate || highSOC) ? 1.0f : 0.0f;
        return true;
    }
    if (id == "summary_status") {
        const bool alarm = commErr || (voltage < th.undervoltage_v) || (voltage > th.overvoltage_v) || (temperature_c > th.overtemp_c);
        value = alarm ? 2.0f : 1.0f;
        return true;
    }

    return false;
}

float applyConversionValue(const VictronFieldConversion& conv, float raw) {
    float value = (raw * conv.gain) + conv.offset;
    if (conv.round) {
        value = static_cast<float>(lrintf(value));
    }
    if (conv.has_min) {
        value = std::max(conv.min_value, value);
    }
    if (conv.has_max) {
        value = std::min(conv.max_value, value);
    }
    return value;
}

bool writeFieldValue(uint8_t* data, const VictronCanFieldDefinition& field, float value) {
    if (field.encoding == VictronFieldEncoding::Bits) {
        if (field.byte_offset >= 8 || field.bit_length == 0 || field.bit_length > 8) {
            return false;
        }
        uint8_t raw = field.conversion.round ? static_cast<uint8_t>(lrintf(value)) : static_cast<uint8_t>(value);
        const uint8_t mask_base = static_cast<uint8_t>((field.bit_length >= 8 ? 0xFFu : ((1u << field.bit_length) - 1u)));
        const uint8_t mask_shifted = static_cast<uint8_t>(mask_base << field.bit_offset);
        const uint8_t value_shifted = static_cast<uint8_t>((raw & mask_base) << field.bit_offset);
        data[field.byte_offset] &= ~mask_shifted;
        data[field.byte_offset] |= value_shifted;
        return true;
    }

    if (field.byte_offset >= 8 || field.length == 0 || field.byte_offset + field.length > 8) {
        return false;
    }

    int32_t raw_int = field.conversion.round ? static_cast<int32_t>(lrintf(value)) : static_cast<int32_t>(value);

    if (field.encoding == VictronFieldEncoding::Unsigned) {
        uint32_t raw = static_cast<uint32_t>(raw_int);
        for (uint8_t i = 0; i < field.length; ++i) {
            data[field.byte_offset + i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFFu);
        }
        return true;
    }

    int32_t raw = raw_int;
    for (uint8_t i = 0; i < field.length; ++i) {
        data[field.byte_offset + i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFFu);
    }
    return true;
}

bool applyVictronMapping(const TinyBMS_Victron_Bridge& bridge, const TinyBMS_LiveData& live, uint16_t pgn, uint8_t* data) {
    const VictronPgnDefinition* def = findVictronPgnDefinition(pgn);
    if (!def) {
        return false;
    }

    BridgeStats local_stats;
    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        local_stats = bridge.stats;
        xSemaphoreGive(statsMutex);
    } else {
        return false; // Cannot proceed without stats
    }

    VictronMappingContext ctx{live, local_stats};
    bool wrote_any = false;

    for (const auto& field : def->fields) {
        float source_value = 0.0f;
        bool has_value = false;

        switch (field.source.type) {
            case VictronValueSourceType::LiveData:
                has_value = getLiveDataValue(field.source.live_field, live, source_value);
                break;
            case VictronValueSourceType::Function:
                has_value = computeFunctionValue(field, bridge, ctx, source_value);
                break;
            case VictronValueSourceType::Constant:
                source_value = field.source.constant;
                has_value = true;
                break;
            default:
                break;
        }

        if (!has_value) {
            continue;
        }

        float converted = applyConversionValue(field.conversion, source_value);
        if (writeFieldValue(data, field, converted)) {
            wrote_any = true;
        }
    }

    return wrote_any;
}

} // namespace

bool TinyBMS_Victron_Bridge::sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc) {
    hal::IHalCan& can_hal = hal::HalManager::instance().can();
    hal::CanFrame frame{};
    frame.id = pgn_id;
    frame.dlc = dlc;
    frame.extended = false;
    memcpy(frame.data.data(), data, dlc);

    bool ok = can_hal.transmit(frame) == hal::Status::Ok;
    hal::CanStats driverStats = can_hal.getStats();
    stats.can_tx_count = driverStats.tx_success;
    stats.can_tx_errors = driverStats.tx_errors;
    stats.can_rx_errors = driverStats.rx_errors;
    stats.can_bus_off_count = driverStats.bus_off_events;
    stats.can_queue_overflows = driverStats.rx_dropped;

    bool log_can_traffic = false;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        log_can_traffic = config.logging.log_can_traffic;
        xSemaphoreGive(configMutex);
    }

    if (ok) {
        if (log_can_traffic) BRIDGE_LOG(LOG_DEBUG, String("TX PGN 0x") + String(pgn_id, HEX));
    } else {
        publishCanAlarm(eventSink(),
                        AlarmCode::CanTxError,
                        "CAN TX failed",
                        AlarmSeverity::Warning,
                        static_cast<float>(pgn_id));
        BRIDGE_LOG(LOG_WARN, String("TX failed PGN 0x") + String(pgn_id, HEX));
    }
    return ok;
}

void TinyBMS_Victron_Bridge::updateEnergyCounters(uint32_t now_ms, const TinyBMS_LiveData& live) {
    if (last_energy_update_ms_ == 0) {
        last_energy_update_ms_ = now_ms;
        return;
    }

    uint32_t delta_ms = now_ms - last_energy_update_ms_;
    last_energy_update_ms_ = now_ms;

    if (delta_ms == 0) {
        return;
    }

    float voltage = live.voltage;
    float current = live.current;
    if (!std::isfinite(voltage) || !std::isfinite(current)) {
        return;
    }
    if (voltage <= 0.1f) {
        return;
    }

    double hours = static_cast<double>(delta_ms) / 3600000.0;
    double power_w = static_cast<double>(voltage) * static_cast<double>(current);
    if (power_w >= 0.0) {
        stats.energy_charged_wh += power_w * hours;
    } else {
        stats.energy_discharged_wh += (-power_w) * hours;
    }

    if (stats.energy_charged_wh < 0.0) {
        stats.energy_charged_wh = 0.0;
    }
    if (stats.energy_discharged_wh < 0.0) {
        stats.energy_discharged_wh = 0.0;
    }
}

void TinyBMS_Victron_Bridge::buildPGN_0x356(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_VOLTAGE_CURRENT, d)) {
        return;
    }

    const auto& ld = live;
    uint16_t u_001V = clamp_u16(round_i(ld.voltage * 100.0f));
    int16_t  i_01A  = clamp_s16(round_i(ld.current * 10.0f));
    int16_t  t_01C  = clamp_s16((int)ld.temperature); // already in 0.1 C
    put_u16_le(&d[0], u_001V);
    put_s16_le(&d[2], i_01A);
    put_s16_le(&d[4], t_01C);
}

void TinyBMS_Victron_Bridge::buildPGN_0x355(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_SOC_SOH, d)) {
        return;
    }

    const auto& ld = live;
    uint16_t soc_01 = clamp_u16(round_i(ld.soc_percent * 10.0f));
    uint16_t soh_01 = clamp_u16(round_i(ld.soh_percent * 10.0f));
    put_u16_le(&d[0], soc_01);
    put_u16_le(&d[2], soh_01);
}

void TinyBMS_Victron_Bridge::buildPGN_0x351(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_CVL_CCL_DCL, d)) {
        return;
    }

    const auto& ld = live;
    float cvl_target_v = stats.cvl_current_v > 0.0f ? stats.cvl_current_v : ld.voltage;
    float ccl_limit_a = stats.ccl_limit_a > 0.0f ? stats.ccl_limit_a : (ld.max_charge_current / 10.0f);
    float dcl_limit_a = stats.dcl_limit_a > 0.0f ? stats.dcl_limit_a : (ld.max_discharge_current / 10.0f);

    uint16_t cvl_001V = clamp_u16(round_i(cvl_target_v * 100.0f));
    uint16_t ccl_01A  = clamp_u16(round_i(ccl_limit_a * 10.0f));
    uint16_t dcl_01A  = clamp_u16(round_i(dcl_limit_a * 10.0f));
    put_u16_le(&d[0], cvl_001V);
    put_u16_le(&d[2], ccl_01A);
    put_u16_le(&d[4], dcl_01A);
    d[6]=d[7]=0;
}

void TinyBMS_Victron_Bridge::buildPGN_0x35A(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_ALARMS, d)) {
        return;
    }

    const auto& ld = live;
    ConfigManager::VictronConfig::Thresholds th{};
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        th = config.victron.thresholds;
        xSemaphoreGive(configMutex);
    }

    const float pack_voltage_v = ld.voltage;
    const float internal_temp_c = ld.temperature / 10.0f;
    const bool has_pack_temp = (ld.findSnapshot(113) != nullptr);
    const float pack_temp_max_c = has_pack_temp ? static_cast<float>(ld.pack_temp_max) / 10.0f : internal_temp_c;
    const float pack_temp_min_c = has_pack_temp ? static_cast<float>(ld.pack_temp_min) / 10.0f : internal_temp_c;
    const bool has_overvoltage_reg = (ld.findSnapshot(315) != nullptr);
    const bool has_undervoltage_reg = (ld.findSnapshot(316) != nullptr);
    const bool has_overheat_reg = (ld.findSnapshot(319) != nullptr);
    const float overheat_cutoff_c = (has_overheat_reg && ld.overheat_cutoff_c > 0)
        ? static_cast<float>(ld.overheat_cutoff_c)
        : th.overtemp_c;
    const uint16_t imbalance = ld.cell_imbalance_mv;

    bool undervoltage_alarm = false;
    if (has_undervoltage_reg && ld.cell_undervoltage_mv > 0 && ld.min_cell_mv > 0) {
        undervoltage_alarm = ld.min_cell_mv <= ld.cell_undervoltage_mv;
    } else {
        undervoltage_alarm = (pack_voltage_v > 0.1f && pack_voltage_v < th.undervoltage_v);
    }

    bool overvoltage_alarm = false;
    if (has_overvoltage_reg && ld.cell_overvoltage_mv > 0 && ld.max_cell_mv > 0) {
        overvoltage_alarm = ld.max_cell_mv >= ld.cell_overvoltage_mv;
    } else {
        overvoltage_alarm = (pack_voltage_v > th.overvoltage_v);
    }

    bool overtemp_alarm = (pack_temp_max_c > overheat_cutoff_c);
    bool low_temp_charge_alarm = (pack_temp_min_c < th.low_temp_charge_c && ld.current > 3.0f);

    uint8_t b0 = 0;
    auto set = [](bool condAlarm, bool condWarn)->uint8_t{
        return condAlarm ? 2 : (condWarn ? 1 : 0);
    };

    b0 = encode2bit(b0, 0, set(undervoltage_alarm, false));
    b0 = encode2bit(b0, 1, set(overvoltage_alarm, false));
    b0 = encode2bit(b0, 2, set(overtemp_alarm, false));
    b0 = encode2bit(b0, 3, set(low_temp_charge_alarm, false));
    d[0] = b0;

    uint8_t b1 = 0;
    b1 = encode2bit(b1, 0, set(imbalance > th.imbalance_alarm_mv, imbalance > th.imbalance_warn_mv));
    bool commErr = (stats.uart_errors > 0 || stats.can_tx_errors > 0 || !stats.victron_keepalive_ok);
    b1 = encode2bit(b1, 1, set(false, commErr));

    bool lowSOC  = (ld.soc_percent <= th.soc_low_percent);
    bool highSOC = (ld.soc_percent >= th.soc_high_percent);
    uint16_t minLimit = static_cast<uint16_t>(th.derate_current_a * 10.0f);
    bool derate = (ld.max_charge_current <= minLimit || ld.max_discharge_current <= minLimit);

    b1 = encode2bit(b1, 2, set(false, lowSOC));
    b1 = encode2bit(b1, 3, set(false, derate || highSOC));
    d[1] = b1;

    d[7] = 0;
    bool global_alarm = commErr || undervoltage_alarm || overvoltage_alarm || overtemp_alarm;
    d[7] = encode2bit(d[7], 0, global_alarm ? 2 : 1);
}

void TinyBMS_Victron_Bridge::buildPGN_0x35E(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_MANUFACTURER, d)) {
        return;
    }
    copyAsciiPadded(d, 8, resolveManufacturerName(live));
}

void TinyBMS_Victron_Bridge::buildPGN_0x35F(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_BATTERY_INFO, d)) {
        return;
    }
    copyAsciiPadded(d, 8, resolveBatteryName(live));
}

void TinyBMS_Victron_Bridge::buildPGN_0x371(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_BMS_NAME_PART2, d)) {
        return;
    }
    copyAsciiPadded(d, 8, resolveBatteryName(live), 8);
}

void TinyBMS_Victron_Bridge::buildPGN_0x378(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_ENERGY_COUNTERS, d)) {
        return;
    }

    uint32_t energy_in_raw = encodeEnergyWh(stats.energy_charged_wh);
    uint32_t energy_out_raw = encodeEnergyWh(stats.energy_discharged_wh);

    put_u32_le(&d[0], energy_in_raw);
    put_u32_le(&d[4], energy_out_raw);
}

void TinyBMS_Victron_Bridge::buildPGN_0x379(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_INSTALLED_CAP, d)) {
        return;
    }

    double capacity_ah = 0.0;
    const TinyRegisterSnapshot* cap_snapshot = live.findSnapshot(306);
    if (cap_snapshot) {
        capacity_ah = static_cast<double>(cap_snapshot->raw_value) * 0.01;
    }
    if (capacity_ah <= 0.0 && config_.battery_capacity_ah > 0.0f) {
        capacity_ah = static_cast<double>(config_.battery_capacity_ah);
    }
    if (capacity_ah < 0.0) {
        capacity_ah = 0.0;
    }
    if (capacity_ah > 65535.0) {
        capacity_ah = 65535.0;
    }

    uint16_t raw_capacity = static_cast<uint16_t>(capacity_ah + 0.5);
    put_u16_le(&d[0], raw_capacity);
}

void TinyBMS_Victron_Bridge::buildPGN_0x382(const TinyBMS_LiveData& live, uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, live, VICTRON_PGN_BATTERY_FAMILY, d)) {
        return;
    }
    copyAsciiPadded(d, 8, resolveBatteryFamily(live));
}

void TinyBMS_Victron_Bridge::canTask(void *pvParameters){
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "canTask started");

    while (true) {
        BridgeEventSink& event_sink = bridge->eventSink();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        bridge->keepAliveProcessRX(now);

        if (now - bridge->last_pgn_update_ms_ >= bridge->pgn_update_interval_ms_) {
            LiveDataUpdate latest{};
            TinyBMS_LiveData live{};
            const bool have_live = event_sink.latest(latest);
            if (have_live) {
                live = latest.data;
                bridge->updateEnergyCounters(now, live);

                uint8_t p[8];

                memset(p, 0, 8); bridge->buildPGN_0x356(live, p); bridge->sendVictronPGN(VICTRON_PGN_VOLTAGE_CURRENT, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x355(live, p); bridge->sendVictronPGN(VICTRON_PGN_SOC_SOH, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x351(live, p); bridge->sendVictronPGN(VICTRON_PGN_CVL_CCL_DCL, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x35A(live, p); bridge->sendVictronPGN(VICTRON_PGN_ALARMS, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x35E(live, p); bridge->sendVictronPGN(VICTRON_PGN_MANUFACTURER, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x35F(live, p); bridge->sendVictronPGN(VICTRON_PGN_BATTERY_INFO, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x371(live, p); bridge->sendVictronPGN(VICTRON_PGN_BMS_NAME_PART2, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x378(live, p); bridge->sendVictronPGN(VICTRON_PGN_ENERGY_COUNTERS, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x379(live, p); bridge->sendVictronPGN(VICTRON_PGN_INSTALLED_CAP, p, 8);
                memset(p, 0, 8); bridge->buildPGN_0x382(live, p); bridge->sendVictronPGN(VICTRON_PGN_BATTERY_FAMILY, p, 8);
            }

            bridge->keepAliveSend();

            bridge->last_pgn_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        // Phase 1: Protect stats writes with statsMutex
        hal::CanStats driverStats = hal::HalManager::instance().can().getStats();
        if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            bridge->stats.can_tx_count = driverStats.tx_success;
            bridge->stats.can_tx_errors = driverStats.tx_errors;
            bridge->stats.can_rx_errors = driverStats.rx_errors;
            bridge->stats.can_bus_off_count = driverStats.bus_off_events;
            bridge->stats.can_queue_overflows = driverStats.rx_dropped;
            xSemaphoreGive(statsMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
