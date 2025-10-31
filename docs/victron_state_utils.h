#pragma once

#include <cstdint>

namespace victron {

struct SystemStateInfo {
    uint8_t code = 0;
    const char* label = "unknown";
};

inline SystemStateInfo mapOnlineStatus(uint16_t status) {
    switch (status) {
        case 0x91:
            return {3u, "charging"};
        case 0x92:
            return {5u, "fully_charged"};
        case 0x93:
            return {9u, "discharging"};
        case 0x96:
            return {3u, "regenerating"};
        case 0x97:
            return {1u, "idle"};
        case 0x9B:
            return {2u, "fault"};
        default:
            break;
    }
    return {0u, "unknown"};
}

} // namespace victron

