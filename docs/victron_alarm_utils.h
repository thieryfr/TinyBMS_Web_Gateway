#pragma once

#include <cstdint>

#include "bridge_pgn_defs.h"
#include "event/event_types_v2.h"

namespace victron {

struct AlarmDescriptor {
    AlarmBit bit = AlarmBit::UnderVoltage;
    const char* path = nullptr;
};

/**
 * @brief Annotate an AlarmEvent with Victron-specific metadata (bit + DBus path).
 *
 * The function preserves backwards compatibility by only populating optional fields
 * when a mapping exists. Existing consumers that ignore the new metadata continue
 * to operate unchanged.
 */
bool annotateAlarm(tinybms::events::AlarmCode code,
                   tinybms::events::AlarmSeverity severity,
                   tinybms::events::AlarmEvent& alarm);

uint8_t severityToVictronLevel(tinybms::events::AlarmSeverity severity);

} // namespace victron

