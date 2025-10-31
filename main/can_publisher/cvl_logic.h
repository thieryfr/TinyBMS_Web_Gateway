#pragma once

#include <stdbool.h>
#include "cvl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float soc_percent;
    unsigned int cell_imbalance_mv;
    float pack_voltage_v;
    float base_ccl_limit_a;
    float base_dcl_limit_a;
    float pack_current_a;
    float max_cell_voltage_v;
} cvl_inputs_t;

typedef struct {
    bool enabled;
    float bulk_soc_threshold;
    float transition_soc_threshold;
    float float_soc_threshold;
    float float_exit_soc;
    float float_approach_offset_mv;
    float float_offset_mv;
    float minimum_ccl_in_float_a;
    unsigned int imbalance_hold_threshold_mv;
    unsigned int imbalance_release_threshold_mv;
    float bulk_target_voltage_v;
    unsigned int series_cell_count;
    float cell_max_voltage_v;
    float cell_safety_threshold_v;
    float cell_safety_release_v;
    float cell_min_float_voltage_v;
    float cell_protection_kp;
    float dynamic_current_nominal_a;
    float max_recovery_step_v;
    float sustain_soc_entry_percent;
    float sustain_soc_exit_percent;
    float sustain_voltage_v;
    float sustain_per_cell_voltage_v;
    float sustain_ccl_limit_a;
    float sustain_dcl_limit_a;
    float imbalance_drop_per_mv;
    float imbalance_drop_max_v;
} cvl_config_snapshot_t;

typedef struct {
    cvl_state_t state;
    float cvl_voltage_v;
    float ccl_limit_a;
    float dcl_limit_a;
    bool imbalance_hold_active;
    bool cell_protection_active;
} cvl_computation_result_t;

typedef struct {
    cvl_state_t state;
    float cvl_voltage_v;
    bool cell_protection_active;
} cvl_runtime_state_t;

void cvl_compute_limits(const cvl_inputs_t *input,
                        const cvl_config_snapshot_t *config,
                        const cvl_runtime_state_t *previous_state,
                        cvl_computation_result_t *out_result);

#ifdef __cplusplus
}
#endif

