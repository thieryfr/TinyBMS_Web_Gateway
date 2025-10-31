#include "cvl_logic.h"

#include <algorithm>

namespace {

float clampNonNegative(float value) {
    return value < 0.0f ? 0.0f : value;
}

} // namespace

CVLComputationResult computeCvlLimits(const CVLInputs& input,
                                      const CVLConfigSnapshot& config,
                                      CVLState previous_state) {
    CVLComputationResult result{};

    // Default passthrough when algorithm is disabled
    if (!config.enabled) {
        result.state = CVL_BULK;
        result.cvl_voltage_v = config.bulk_target_voltage_v;
        result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
        result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
        result.imbalance_hold_active = false;
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

    bool imbalance_hold = (previous_state == CVL_IMBALANCE_HOLD);
    if (imbalance_hold) {
        if (input.cell_imbalance_mv <= config.imbalance_release_threshold_mv) {
            imbalance_hold = false;
        }
    } else if (input.cell_imbalance_mv > config.imbalance_hold_threshold_mv) {
        imbalance_hold = true;
    }

    if (imbalance_hold) {
        result.state = CVL_IMBALANCE_HOLD;
        result.cvl_voltage_v = float_approach;
        const float min_ccl = std::max(config.minimum_ccl_in_float_a, 0.0f);
        result.ccl_limit_a = std::min(clampNonNegative(input.base_ccl_limit_a), min_ccl > 0.0f ? min_ccl : clampNonNegative(input.base_ccl_limit_a));
        result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
        result.imbalance_hold_active = true;
        return result;
    }

    const float soc = input.soc_percent;
    CVLState state = CVL_BULK;

    if (previous_state == CVL_FLOAT && soc >= config.float_exit_soc) {
        state = CVL_FLOAT;
    } else {
        if (soc >= config.float_soc_threshold) {
            state = CVL_FLOAT;
        } else if (soc >= config.transition_soc_threshold) {
            state = CVL_FLOAT_APPROACH;
        } else if (soc >= config.bulk_soc_threshold) {
            state = CVL_TRANSITION;
        }

        if (state == CVL_FLOAT_APPROACH && previous_state == CVL_FLOAT_APPROACH &&
            soc + 0.25f < config.transition_soc_threshold) {
            state = CVL_TRANSITION;
        }
    }

    result.state = state;
    result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
    result.imbalance_hold_active = false;

    switch (state) {
        case CVL_BULK:
        case CVL_TRANSITION:
            result.cvl_voltage_v = bulk_target;
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;

        case CVL_FLOAT_APPROACH:
            result.cvl_voltage_v = float_approach;
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;

        case CVL_FLOAT: {
            result.cvl_voltage_v = float_voltage;
            const float min_ccl = std::max(config.minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = std::min(clampNonNegative(input.base_ccl_limit_a), min_ccl);
            } else {
                result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            }
            break;
        }

        default:
            result.cvl_voltage_v = bulk_target;
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;
    }

    return result;
}

