#include "cvl_logic.h"

#include <algorithm>

namespace {

float clampNonNegative(float value) {
    return value < 0.0f ? 0.0f : value;
}

float computeSustainVoltage(const CVLConfigSnapshot& config) {
    if (config.sustain_voltage_v > 0.0f) {
        return config.sustain_voltage_v;
    }
    if (config.series_cell_count == 0) {
        return 0.0f;
    }
    return config.sustain_per_cell_voltage_v * static_cast<float>(config.series_cell_count);
}

float computeAbsMaxVoltage(const CVLConfigSnapshot& config) {
    if (config.series_cell_count == 0) {
        return config.bulk_target_voltage_v;
    }
    return config.cell_max_voltage_v * static_cast<float>(config.series_cell_count);
}

float computeMinFloatVoltage(const CVLConfigSnapshot& config) {
    if (config.series_cell_count == 0) {
        return 0.0f;
    }
    return config.cell_min_float_voltage_v * static_cast<float>(config.series_cell_count);
}

} // namespace

CVLComputationResult computeCvlLimits(const CVLInputs& input,
                                      const CVLConfigSnapshot& config,
                                      const CVLRuntimeState& previous_state) {
    CVLComputationResult result{};

    // Default passthrough when algorithm is disabled
    if (!config.enabled) {
        result.state = CVL_BULK;
        result.cvl_voltage_v = config.bulk_target_voltage_v;
        result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
        result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
        result.imbalance_hold_active = false;
        result.cell_protection_active = false;
        return result;
    }

    const float bulk_target = std::max(config.bulk_target_voltage_v, 0.0f);
    float float_approach = bulk_target - (config.float_approach_offset_mv / 1000.0f);
    float float_voltage = bulk_target - (config.float_offset_mv / 1000.0f);
    float_approach = std::max(float_approach, 0.0f);
    float_voltage = std::max(float_voltage, 0.0f);

    if (float_voltage > float_approach) {
        std::swap(float_voltage, float_approach);
    }

    const float soc = input.soc_percent;
    CVLState state = previous_state.state;

    const bool sustain_supported = config.sustain_soc_exit_percent > config.sustain_soc_entry_percent;
    bool sustain_active = (previous_state.state == CVL_SUSTAIN);
    if (sustain_supported) {
        if (!sustain_active && soc <= config.sustain_soc_entry_percent) {
            sustain_active = true;
        } else if (sustain_active && soc >= config.sustain_soc_exit_percent) {
            sustain_active = false;
        }
    } else {
        sustain_active = false;
    }

    bool imbalance_hold = (previous_state.state == CVL_IMBALANCE_HOLD) && !sustain_active;
    if (imbalance_hold) {
        if (input.cell_imbalance_mv <= config.imbalance_release_threshold_mv) {
            imbalance_hold = false;
        }
    } else if (!sustain_active && input.cell_imbalance_mv > config.imbalance_hold_threshold_mv) {
        imbalance_hold = true;
    }

    if (sustain_active) {
        state = CVL_SUSTAIN;
    } else if (imbalance_hold) {
        state = CVL_IMBALANCE_HOLD;
    } else {
        if (previous_state.state == CVL_FLOAT && soc >= config.float_exit_soc) {
            state = CVL_FLOAT;
        } else {
            if (soc >= config.float_soc_threshold) {
                state = CVL_FLOAT;
            } else if (soc >= config.transition_soc_threshold) {
                state = CVL_FLOAT_APPROACH;
            } else if (soc >= config.bulk_soc_threshold) {
                state = CVL_TRANSITION;
            } else {
                state = CVL_BULK;
            }

            if (state == CVL_FLOAT_APPROACH && previous_state.state == CVL_FLOAT_APPROACH &&
                soc + 0.25f < config.transition_soc_threshold) {
                state = CVL_TRANSITION;
            }
        }
    }

    result.state = state;
    result.imbalance_hold_active = (state == CVL_IMBALANCE_HOLD);

    const float base_ccl = clampNonNegative(input.base_ccl_limit_a);
    const float base_dcl = clampNonNegative(input.base_dcl_limit_a);
    result.ccl_limit_a = base_ccl;
    result.dcl_limit_a = base_dcl;

    float state_cvl = bulk_target;

    switch (state) {
        case CVL_BULK:
        case CVL_TRANSITION:
            state_cvl = bulk_target;
            break;

        case CVL_FLOAT_APPROACH:
            state_cvl = float_approach;
            break;

        case CVL_FLOAT: {
            state_cvl = float_voltage;
            const float min_ccl = std::max(config.minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = std::min(base_ccl, min_ccl);
            }
            break;
        }

        case CVL_IMBALANCE_HOLD: {
            const float min_float = computeMinFloatVoltage(config);
            const int32_t over_threshold = static_cast<int32_t>(input.cell_imbalance_mv) -
                                           static_cast<int32_t>(config.imbalance_hold_threshold_mv);
            const float drop = std::min(config.imbalance_drop_max_v,
                                        std::max(0.0f, static_cast<float>(over_threshold)) *
                                            config.imbalance_drop_per_mv);
            state_cvl = std::max(bulk_target - drop, min_float);
            const float min_ccl = std::max(config.minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = std::min(base_ccl, min_ccl);
            }
            break;
        }

        case CVL_SUSTAIN: {
            const float sustain_voltage = std::max(computeSustainVoltage(config), computeMinFloatVoltage(config));
            state_cvl = sustain_voltage;
            result.ccl_limit_a = std::min(base_ccl, config.sustain_ccl_limit_a);
            result.dcl_limit_a = std::min(base_dcl, config.sustain_dcl_limit_a);
            break;
        }
    }

    float cell_limit = computeAbsMaxVoltage(config);
    bool cell_protection_active = false;

    if (config.series_cell_count > 0 && config.cell_max_voltage_v > 0.0f) {
        bool protection_active = previous_state.cell_protection_active;
        if (!protection_active && input.max_cell_voltage_v >= config.cell_safety_threshold_v) {
            protection_active = true;
        } else if (protection_active &&
                   input.max_cell_voltage_v <= config.cell_safety_release_v) {
            protection_active = false;
        }

        const float min_float = computeMinFloatVoltage(config);
        if (protection_active) {
            const float delta_v = std::max(0.0f, input.max_cell_voltage_v - config.cell_safety_threshold_v);
            const float charge_current = std::max(0.0f, input.pack_current_a);
            const float nominal_current = std::max(config.dynamic_current_nominal_a, 1.0f);
            const float current_factor = 1.0f + (charge_current / nominal_current);
            const float reduction = config.cell_protection_kp * current_factor * delta_v;
            cell_limit = std::max(min_float, cell_limit - reduction);
        } else {
            cell_limit = std::max(min_float, cell_limit);
        }

        if (config.max_recovery_step_v > 0.0f && previous_state.cvl_voltage_v > 0.0f &&
            (protection_active || previous_state.cell_protection_active)) {
            cell_limit = std::min(cell_limit, previous_state.cvl_voltage_v + config.max_recovery_step_v);
        }

        cell_protection_active = protection_active;
    }

    const float final_cvl = std::min(state_cvl, cell_limit);
    float ratio = 1.0f;
    if (state_cvl > 0.0f) {
        ratio = final_cvl / state_cvl;
        if (ratio < 0.0f) {
            ratio = 0.0f;
        } else if (ratio > 1.0f) {
            ratio = 1.0f;
        }
    }

    result.cvl_voltage_v = final_cvl;
    const float scaled_ccl = result.ccl_limit_a * ratio;
    const float scaled_dcl = result.dcl_limit_a * ratio;
    result.ccl_limit_a = std::min(result.ccl_limit_a, scaled_ccl);
    result.dcl_limit_a = std::min(result.dcl_limit_a, scaled_dcl);
    result.cell_protection_active = cell_protection_active;

    return result;
}

