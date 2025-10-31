#pragma once

#include <cstdint>
#include "cvl_types.h"

struct CVLInputs {
    float soc_percent = 0.0f;
    uint16_t cell_imbalance_mv = 0;
    float pack_voltage_v = 0.0f;
    float base_ccl_limit_a = 0.0f;
    float base_dcl_limit_a = 0.0f;
    float pack_current_a = 0.0f;
    float max_cell_voltage_v = 0.0f;
};

struct CVLConfigSnapshot {
    bool enabled = true;
    float bulk_soc_threshold = 90.0f;
    float transition_soc_threshold = 95.0f;
    float float_soc_threshold = 98.0f;
    float float_exit_soc = 95.0f;
    float float_approach_offset_mv = 50.0f;
    float float_offset_mv = 100.0f;
    float minimum_ccl_in_float_a = 5.0f;
    uint16_t imbalance_hold_threshold_mv = 100;
    uint16_t imbalance_release_threshold_mv = 50;
    float bulk_target_voltage_v = 0.0f;
    uint16_t series_cell_count = 16;
    float cell_max_voltage_v = 3.65f;
    float cell_safety_threshold_v = 3.50f;
    float cell_safety_release_v = 3.47f;
    float cell_min_float_voltage_v = 3.20f;
    float cell_protection_kp = 120.0f;
    float dynamic_current_nominal_a = 157.0f;
    float max_recovery_step_v = 0.4f;
    float sustain_soc_entry_percent = 5.0f;
    float sustain_soc_exit_percent = 8.0f;
    float sustain_voltage_v = 0.0f;
    float sustain_per_cell_voltage_v = 3.125f;
    float sustain_ccl_limit_a = 5.0f;
    float sustain_dcl_limit_a = 5.0f;
    float imbalance_drop_per_mv = 0.0005f;
    float imbalance_drop_max_v = 2.0f;
};

struct CVLComputationResult {
    CVLState state = CVL_BULK;
    float cvl_voltage_v = 0.0f;
    float ccl_limit_a = 0.0f;
    float dcl_limit_a = 0.0f;
    bool imbalance_hold_active = false;
    bool cell_protection_active = false;
};

struct CVLRuntimeState {
    CVLState state = CVL_BULK;
    float cvl_voltage_v = 0.0f;
    bool cell_protection_active = false;
};

CVLComputationResult computeCvlLimits(const CVLInputs& input,
                                      const CVLConfigSnapshot& config,
                                      const CVLRuntimeState& previous_state);

