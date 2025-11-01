/**
 * @file bridge_cvl.cpp
 * @brief CVL task implementation
 */

#include <Arduino.h>
#include <algorithm>

#include "bridge_cvl.h"
#include "logger.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "config_manager.h"
#include "cvl_logic.h"
#include "event/event_types_v2.h"

#include <cstring>

using tinybms::events::CVLStateChanged;
using tinybms::events::EventSource;
using tinybms::events::LiveDataUpdate;

extern Logger logger;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CVL] ") + (msg)); } while(0)

namespace {

    CVLConfigSnapshot loadConfigSnapshot(const TinyBMS_LiveData& data) {
        CVLConfigSnapshot snapshot;
        snapshot.bulk_target_voltage_v = data.voltage;
        snapshot.series_cell_count = 16;

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            snapshot.enabled = config.cvl.enabled;
            snapshot.bulk_soc_threshold = config.cvl.bulk_soc_threshold;
            snapshot.transition_soc_threshold = config.cvl.transition_soc_threshold;
            snapshot.float_soc_threshold = config.cvl.float_soc_threshold;
            snapshot.float_exit_soc = config.cvl.float_exit_soc;
            snapshot.float_approach_offset_mv = config.cvl.float_approach_offset_mv;
            snapshot.float_offset_mv = config.cvl.float_offset_mv;
            snapshot.minimum_ccl_in_float_a = config.cvl.minimum_ccl_in_float_a;
            snapshot.imbalance_hold_threshold_mv = config.cvl.imbalance_hold_threshold_mv;
            snapshot.imbalance_release_threshold_mv = config.cvl.imbalance_release_threshold_mv;
            snapshot.bulk_target_voltage_v = config.victron.thresholds.overvoltage_v;
            snapshot.series_cell_count = config.cvl.series_cell_count;
            snapshot.cell_max_voltage_v = config.cvl.cell_max_voltage_v;
            snapshot.cell_safety_threshold_v = config.cvl.cell_safety_threshold_v;
            snapshot.cell_safety_release_v = config.cvl.cell_safety_release_v;
            snapshot.cell_min_float_voltage_v = config.cvl.cell_min_float_voltage_v;
            snapshot.cell_protection_kp = config.cvl.cell_protection_kp;
            snapshot.dynamic_current_nominal_a = config.cvl.dynamic_current_nominal_a;
            snapshot.max_recovery_step_v = config.cvl.max_recovery_step_v;
            snapshot.sustain_soc_entry_percent = config.cvl.sustain_soc_entry_percent;
            snapshot.sustain_soc_exit_percent = config.cvl.sustain_soc_exit_percent;
            snapshot.sustain_voltage_v = config.cvl.sustain_voltage_v;
            snapshot.sustain_per_cell_voltage_v = config.cvl.sustain_per_cell_voltage_v;
            snapshot.sustain_ccl_limit_a = config.cvl.sustain_ccl_limit_a;
            snapshot.sustain_dcl_limit_a = config.cvl.sustain_dcl_limit_a;
            snapshot.imbalance_drop_per_mv = config.cvl.imbalance_drop_per_mv;
            snapshot.imbalance_drop_max_v = config.cvl.imbalance_drop_max_v;
            if (snapshot.bulk_target_voltage_v <= 0.0f) {
                snapshot.bulk_target_voltage_v = data.voltage;
            }
            xSemaphoreGive(configMutex);
        }

        return snapshot;
    }

bool shouldLogChanges() {
    bool enabled = false;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        enabled = config.logging.log_cvl_changes;
        xSemaphoreGive(configMutex);
    }
    return enabled;
}

void logChangeIfNeeded(const CVLComputationResult& result,
                       CVLState previous_state,
                       const TinyBMS_LiveData& data) {
    if (!shouldLogChanges()) return;

    BRIDGE_LOG(LOG_INFO,
               String("State ") + previous_state + " → " + result.state +
               ", CVL=" + String(result.cvl_voltage_v, 2) + "V, " +
               "CCL=" + String(result.ccl_limit_a, 2) + "A, " +
               "DCL=" + String(result.dcl_limit_a, 2) + "A, " +
               "SOC=" + String(data.soc_percent, 1) + "%");
}

} // namespace

void TinyBMS_Victron_Bridge::cvlTask(void *pvParameters){
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "cvlTask started");

    // Phase 1: Read initial CVL state with statsMutex protection
    CVLState last_state = CVL_BULK;
    if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        last_state = bridge->stats.cvl_state;
        xSemaphoreGive(statsMutex);
    }
    uint32_t state_entry_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (true) {
        BridgeEventSink& event_sink = bridge->eventSink();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_cvl_update_ms_ >= bridge->cvl_update_interval_ms_) {
            LiveDataUpdate latest_live{};
            if (event_sink.latest(latest_live)) {
                const TinyBMS_LiveData& data = latest_live.data;
                CVLInputs inputs;
                inputs.soc_percent = data.soc_percent;
                inputs.cell_imbalance_mv = data.cell_imbalance_mv;
                inputs.pack_voltage_v = data.voltage;
                inputs.base_ccl_limit_a = data.max_charge_current / 10.0f;
                inputs.base_dcl_limit_a = data.max_discharge_current / 10.0f;
                inputs.pack_current_a = data.current;
                inputs.max_cell_voltage_v = data.max_cell_mv / 1000.0f;

                CVLConfigSnapshot snapshot = loadConfigSnapshot(data);
                if (snapshot.bulk_target_voltage_v <= 0.0f) {
                    snapshot.bulk_target_voltage_v = std::max(inputs.pack_voltage_v, 0.0f);
                }

                // Phase 1: Read previous runtime state with statsMutex protection
                CVLRuntimeState previous_runtime;
                previous_runtime.state = last_state;
                if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    previous_runtime.cvl_voltage_v = bridge->stats.cvl_current_v;
                    previous_runtime.cell_protection_active = bridge->stats.cell_protection_active;
                    xSemaphoreGive(statsMutex);
                } else {
                    previous_runtime.cvl_voltage_v = 0.0f;
                    previous_runtime.cell_protection_active = false;
                }

                CVLComputationResult result = computeCvlLimits(inputs, snapshot, previous_runtime);

                // Phase 1: Write new CVL state with statsMutex protection
                if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    bridge->stats.cvl_state = result.state;
                    bridge->stats.cvl_current_v = result.cvl_voltage_v;
                    bridge->stats.ccl_limit_a = result.ccl_limit_a;
                    bridge->stats.dcl_limit_a = result.dcl_limit_a;
                    bridge->stats.cell_protection_active = result.cell_protection_active;
                    xSemaphoreGive(statsMutex);
                }

                if (result.state != last_state) {
                    uint32_t duration = now - state_entry_ms;
                    CVLStateChanged event{};
                    event.metadata.source = EventSource::Cvl;
                    event.state.old_state = static_cast<uint8_t>(last_state);
                    event.state.new_state = static_cast<uint8_t>(result.state);
                    event.state.new_cvl_voltage = result.cvl_voltage_v;
                    event.state.new_ccl_current = result.ccl_limit_a;
                    event.state.new_dcl_current = result.dcl_limit_a;
                    event.state.state_duration_ms = duration;
                    event_sink.publish(event);
                    logChangeIfNeeded(result, last_state, data);
                    last_state = result.state;
                    state_entry_ms = now;
                }

                logger.log(LOG_DEBUG,
                           String("[CVL] target=") + String(result.cvl_voltage_v, 2) +
                           "V CCL=" + String(result.ccl_limit_a, 1) +
                           "A DCL=" + String(result.dcl_limit_a, 1) + "A");
            }

            bridge->last_cvl_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(bridge->cvl_update_interval_ms_));
    }
}

