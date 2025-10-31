#include "cvl_logic.h"

#include <math.h>
#include <stddef.h>

static float clamp_non_negative(float value)
{
    return (value < 0.0f) ? 0.0f : value;
}

static float compute_sustain_voltage(const cvl_config_snapshot_t *config)
{
    if (config == NULL) {
        return 0.0f;
    }

    if (config->sustain_voltage_v > 0.0f) {
        return config->sustain_voltage_v;
    }

    if (config->series_cell_count == 0U) {
        return 0.0f;
    }

    return config->sustain_per_cell_voltage_v * (float)config->series_cell_count;
}

static float compute_abs_max_voltage(const cvl_config_snapshot_t *config)
{
    if (config == NULL) {
        return 0.0f;
    }

    if (config->series_cell_count == 0U) {
        return config->bulk_target_voltage_v;
    }

    return config->cell_max_voltage_v * (float)config->series_cell_count;
}

static float compute_min_float_voltage(const cvl_config_snapshot_t *config)
{
    if (config == NULL) {
        return 0.0f;
    }

    if (config->series_cell_count == 0U) {
        return 0.0f;
    }

    return config->cell_min_float_voltage_v * (float)config->series_cell_count;
}

static float clamp_ratio(float numerator, float denominator)
{
    if (denominator <= 0.0f) {
        return 1.0f;
    }

    float ratio = numerator / denominator;
    if (ratio < 0.0f) {
        return 0.0f;
    }
    if (ratio > 1.0f) {
        return 1.0f;
    }
    return ratio;
}

void cvl_compute_limits(const cvl_inputs_t *input,
                        const cvl_config_snapshot_t *config,
                        const cvl_runtime_state_t *previous_state,
                        cvl_computation_result_t *out_result)
{
    if (out_result == NULL) {
        return;
    }

    cvl_computation_result_t result = {
        .state = CVL_STATE_BULK,
        .cvl_voltage_v = 0.0f,
        .ccl_limit_a = 0.0f,
        .dcl_limit_a = 0.0f,
        .imbalance_hold_active = false,
        .cell_protection_active = false,
    };

    if (input == NULL || config == NULL || previous_state == NULL) {
        *out_result = result;
        return;
    }

    if (!config->enabled) {
        result.state = CVL_STATE_BULK;
        result.cvl_voltage_v = config->bulk_target_voltage_v;
        result.ccl_limit_a = clamp_non_negative(input->base_ccl_limit_a);
        result.dcl_limit_a = clamp_non_negative(input->base_dcl_limit_a);
        *out_result = result;
        return;
    }

    const float bulk_target = fmaxf(config->bulk_target_voltage_v, 0.0f);
    float float_approach = bulk_target - (config->float_approach_offset_mv / 1000.0f);
    float float_voltage = bulk_target - (config->float_offset_mv / 1000.0f);
    float_approach = fmaxf(float_approach, 0.0f);
    float_voltage = fmaxf(float_voltage, 0.0f);

    if (float_voltage > float_approach) {
        float tmp = float_voltage;
        float_voltage = float_approach;
        float_approach = tmp;
    }

    const float soc = input->soc_percent;
    cvl_state_t state = previous_state->state;

    const bool sustain_supported = (config->sustain_soc_exit_percent > config->sustain_soc_entry_percent);
    bool sustain_active = (previous_state->state == CVL_STATE_SUSTAIN);
    if (sustain_supported) {
        if (!sustain_active && soc <= config->sustain_soc_entry_percent) {
            sustain_active = true;
        } else if (sustain_active && soc >= config->sustain_soc_exit_percent) {
            sustain_active = false;
        }
    } else {
        sustain_active = false;
    }

    bool imbalance_hold = (previous_state->state == CVL_STATE_IMBALANCE_HOLD) && !sustain_active;
    if (imbalance_hold) {
        if (input->cell_imbalance_mv <= config->imbalance_release_threshold_mv) {
            imbalance_hold = false;
        }
    } else if (!sustain_active && input->cell_imbalance_mv > config->imbalance_hold_threshold_mv) {
        imbalance_hold = true;
    }

    if (sustain_active) {
        state = CVL_STATE_SUSTAIN;
    } else if (imbalance_hold) {
        state = CVL_STATE_IMBALANCE_HOLD;
    } else {
        if (previous_state->state == CVL_STATE_FLOAT && soc >= config->float_exit_soc) {
            state = CVL_STATE_FLOAT;
        } else {
            if (soc >= config->float_soc_threshold) {
                state = CVL_STATE_FLOAT;
            } else if (soc >= config->transition_soc_threshold) {
                state = CVL_STATE_FLOAT_APPROACH;
            } else if (soc >= config->bulk_soc_threshold) {
                state = CVL_STATE_TRANSITION;
            } else {
                state = CVL_STATE_BULK;
            }

            if (state == CVL_STATE_FLOAT_APPROACH && previous_state->state == CVL_STATE_FLOAT_APPROACH &&
                (soc + 0.25f) < config->transition_soc_threshold) {
                state = CVL_STATE_TRANSITION;
            }
        }
    }

    result.state = state;
    result.imbalance_hold_active = (state == CVL_STATE_IMBALANCE_HOLD);

    const float base_ccl = clamp_non_negative(input->base_ccl_limit_a);
    const float base_dcl = clamp_non_negative(input->base_dcl_limit_a);
    result.ccl_limit_a = base_ccl;
    result.dcl_limit_a = base_dcl;

    float state_cvl = bulk_target;

    switch (state) {
        case CVL_STATE_BULK:
        case CVL_STATE_TRANSITION:
            state_cvl = bulk_target;
            break;
        case CVL_STATE_FLOAT_APPROACH:
            state_cvl = float_approach;
            break;
        case CVL_STATE_FLOAT: {
            state_cvl = float_voltage;
            const float min_ccl = fmaxf(config->minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = fminf(base_ccl, min_ccl);
            }
            break;
        }
        case CVL_STATE_IMBALANCE_HOLD: {
            const float min_float = compute_min_float_voltage(config);
            const int32_t over_threshold = (int32_t)input->cell_imbalance_mv -
                                           (int32_t)config->imbalance_hold_threshold_mv;
            float drop = (float)over_threshold;
            if (drop < 0.0f) {
                drop = 0.0f;
            }
            drop = fminf(config->imbalance_drop_max_v, drop * config->imbalance_drop_per_mv);
            state_cvl = fmaxf(bulk_target - drop, min_float);
            const float min_ccl = fmaxf(config->minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = fminf(base_ccl, min_ccl);
            }
            break;
        }
        case CVL_STATE_SUSTAIN: {
            const float sustain_voltage = fmaxf(compute_sustain_voltage(config), compute_min_float_voltage(config));
            state_cvl = sustain_voltage;
            result.ccl_limit_a = fminf(base_ccl, config->sustain_ccl_limit_a);
            result.dcl_limit_a = fminf(base_dcl, config->sustain_dcl_limit_a);
            break;
        }
        default:
            break;
    }

    float cell_limit = compute_abs_max_voltage(config);
    bool cell_protection_active = false;

    if (config->series_cell_count > 0U && config->cell_max_voltage_v > 0.0f) {
        bool protection_active = previous_state->cell_protection_active;
        if (!protection_active && input->max_cell_voltage_v >= config->cell_safety_threshold_v) {
            protection_active = true;
        } else if (protection_active && input->max_cell_voltage_v <= config->cell_safety_release_v) {
            protection_active = false;
        }

        const float min_float = compute_min_float_voltage(config);
        if (protection_active) {
            const float delta_v = fmaxf(0.0f, input->max_cell_voltage_v - config->cell_safety_threshold_v);
            const float charge_current = fmaxf(0.0f, input->pack_current_a);
            const float nominal_current = fmaxf(config->dynamic_current_nominal_a, 1.0f);
            const float current_factor = 1.0f + (charge_current / nominal_current);
            const float reduction = config->cell_protection_kp * current_factor * delta_v;
            cell_limit = fmaxf(min_float, cell_limit - reduction);
        } else {
            cell_limit = fmaxf(min_float, cell_limit);
        }

        if (config->max_recovery_step_v > 0.0f && previous_state->cvl_voltage_v > 0.0f &&
            (protection_active || previous_state->cell_protection_active)) {
            cell_limit = fminf(cell_limit, previous_state->cvl_voltage_v + config->max_recovery_step_v);
        }

        cell_protection_active = protection_active;
    }

    const float final_cvl = fminf(state_cvl, cell_limit);
    const float ratio = clamp_ratio(final_cvl, state_cvl);

    result.cvl_voltage_v = final_cvl;
    result.ccl_limit_a = fminf(result.ccl_limit_a, result.ccl_limit_a * ratio);
    result.dcl_limit_a = fminf(result.dcl_limit_a, result.dcl_limit_a * ratio);
    result.cell_protection_active = cell_protection_active;

    *out_result = result;
}

