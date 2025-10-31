
/**
 * @file bridge_pgn_defs.h
 * @brief Victron CAN-BMS PGN & scaling definitions + 0x35A bit encoding
 * @version 2.4.0
 */
#pragma once

// ---- Standard 11-bit IDs used by Victron CAN-bus BMS
#define VICTRON_PGN_CVL_CCL_DCL      0x351
#define VICTRON_PGN_SOC_SOH          0x355
#define VICTRON_PGN_VOLTAGE_CURRENT  0x356
#define VICTRON_PGN_ALARMS           0x35A
#define VICTRON_PGN_MANUFACTURER     0x35E
#define VICTRON_PGN_BATTERY_INFO     0x35F
#define VICTRON_PGN_BMS_NAME_PART2   0x371
#define VICTRON_PGN_ENERGY_COUNTERS  0x378
#define VICTRON_PGN_INSTALLED_CAP    0x379
#define VICTRON_PGN_BATTERY_FAMILY   0x382
#define VICTRON_PGN_KEEPALIVE        0x305

// ---- Scaling
// voltage: 0.01V → ×100
// current: 0.1A   → ×10 (signed)
// temp:    0.1°C  → value already in 0.1 steps
// soc/soh: 0.1%   → ×10
// ccl/dcl: 0.1A   → native 0.1A fields
// cvl:     0.01V  → ×100

// ---- 0x35A encoding (2-bit fields). We pack 4 fields per byte when needed.
// For simplicity we provide bit helpers for Alarm/Warning bytes used here.
enum class AlarmBit : uint8_t {
    UnderVoltage     = 0,  // Byte0 bit0
    OverVoltage      = 1,  // Byte0 bit1
    OverTemperature  = 2,  // Byte0 bit2
    LowTempCharge    = 3,  // Byte0 bit3
    CellImbalance    = 4,  // Byte0 bit4
    CommsError       = 5,  // Byte0 bit5
    Reserved6        = 6,  // Byte0 bit6
    Shutdown         = 7   // Byte0 bit7
};

enum class WarnBit : uint8_t {
    LowSOC         = 0, // Byte1 bit0
    HighSOC        = 1, // Byte1 bit1
    Derating       = 2, // Byte1 bit2
    InfoIdleCharge = 3  // Byte1 bit3
};

// States per 2-bit field (00 = normal/no data, 01 = warning, 10 = alarm, 11 = reserved)
// We encode here as: 0=no, 1=warning, 2=alarm.
inline uint8_t encode2bit(uint8_t current, uint8_t index, uint8_t level) {
    // index ∈ [0..3] for a single byte packing, level ∈ {0,1,2}
    const uint8_t shift = (index & 0x3u) * 2;
    current &= ~(0x3u << shift);
    current |= ((level & 0x3u) << shift);
    return current;
}
