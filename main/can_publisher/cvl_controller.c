#include "cvl_controller.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef CVL_DEFAULT_SERIES_CELLS
#define CVL_DEFAULT_SERIES_CELLS 16U
#endif

static const cvl_config_snapshot_t s_cvl_default_config = {
    .enabled = true,
    .bulk_soc_threshold = 90.0f,
    .transition_soc_threshold = 95.0f,
    .float_soc_threshold = 98.0f,
    .float_exit_soc = 95.0f,
    .float_approach_offset_mv = 50.0f,
    .float_offset_mv = 100.0f,
    .minimum_ccl_in_float_a = 5.0f,
    .imbalance_hold_threshold_mv = 100U,
    .imbalance_release_threshold_mv = 50U,
    .bulk_target_voltage_v = 0.0f,
    .series_cell_count = CVL_DEFAULT_SERIES_CELLS,
    .cell_max_voltage_v = 3.65f,
    .cell_safety_threshold_v = 3.50f,
    .cell_safety_release_v = 3.47f,
    .cell_min_float_voltage_v = 3.20f,
    .cell_protection_kp = 120.0f,
    .dynamic_current_nominal_a = 157.0f,
    .max_recovery_step_v = 0.4f,
    .sustain_soc_entry_percent = 5.0f,
    .sustain_soc_exit_percent = 8.0f,
    .sustain_voltage_v = 0.0f,
    .sustain_per_cell_voltage_v = 3.125f,
    .sustain_ccl_limit_a = 5.0f,
    .sustain_dcl_limit_a = 5.0f,
    .imbalance_drop_per_mv = 0.0005f,
    .imbalance_drop_max_v = 2.0f,
};

static cvl_runtime_state_t s_cvl_runtime = {
    .state = CVL_STATE_BULK,
    .cvl_voltage_v = 0.0f,
    .cell_protection_active = false,
};

static can_publisher_cvl_result_t s_cvl_result = {
    .timestamp_ms = 0,
    .result = {
        .state = CVL_STATE_BULK,
        .cvl_voltage_v = 0.0f,
        .ccl_limit_a = 0.0f,
        .dcl_limit_a = 0.0f,
        .imbalance_hold_active = false,
        .cell_protection_active = false,
    },
};

static bool s_cvl_has_result = false;
static bool s_cvl_initialised = false;

static float safe_float(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    return value;
}

static float fallback_float(float preferred, float fallback)
{
    if (!isfinite(preferred) || preferred <= 0.0f) {
        return fallback;
    }
    return preferred;
}

static unsigned int fallback_unsigned(unsigned int preferred, unsigned int fallback)
{
    return (preferred > 0U) ? preferred : fallback;
}

void can_publisher_cvl_init(void)
{
    s_cvl_runtime.state = CVL_STATE_BULK;
    s_cvl_runtime.cvl_voltage_v = 0.0f;
    s_cvl_runtime.cell_protection_active = false;
    s_cvl_result.timestamp_ms = 0;
    s_cvl_result.result.state = CVL_STATE_BULK;
    s_cvl_result.result.cvl_voltage_v = 0.0f;
    s_cvl_result.result.ccl_limit_a = 0.0f;
    s_cvl_result.result.dcl_limit_a = 0.0f;
    s_cvl_result.result.imbalance_hold_active = false;
    s_cvl_result.result.cell_protection_active = false;
    s_cvl_has_result = false;
    s_cvl_initialised = true;
}

static cvl_config_snapshot_t cvl_controller_load_config(const uart_bms_live_data_t *data)
{
    cvl_config_snapshot_t config = s_cvl_default_config;

    if (data != NULL) {
        float bulk_target = config.bulk_target_voltage_v;
        if (data->overvoltage_cutoff_mv > 0U) {
            bulk_target = (float)data->overvoltage_cutoff_mv / 1000.0f;
        }
        bulk_target = fallback_float(bulk_target, data->pack_voltage_v);
        config.bulk_target_voltage_v = bulk_target;

        unsigned int series_cells = fallback_unsigned(data->series_cell_count, config.series_cell_count);
        config.series_cell_count = series_cells;
    }

    return config;
}

static void cvl_controller_prepare_inputs(const uart_bms_live_data_t *data, cvl_inputs_t *inputs)
{
    if (inputs == NULL) {
        return;
    }

    memset(inputs, 0, sizeof(*inputs));
    if (data == NULL) {
        return;
    }

    inputs->soc_percent = safe_float(data->state_of_charge_pct);
    if (inputs->soc_percent < 0.0f) {
        inputs->soc_percent = 0.0f;
    }

    if (data->max_cell_mv > 0U && data->min_cell_mv > 0U && data->max_cell_mv >= data->min_cell_mv) {
        inputs->cell_imbalance_mv = (unsigned int)(data->max_cell_mv - data->min_cell_mv);
    } else {
        inputs->cell_imbalance_mv = 0U;
    }

    inputs->pack_voltage_v = safe_float(data->pack_voltage_v);
    if (inputs->pack_voltage_v < 0.0f) {
        inputs->pack_voltage_v = 0.0f;
    }

    inputs->pack_current_a = safe_float(data->pack_current_a);

    float base_ccl = safe_float(data->charge_overcurrent_limit_a);
    if (base_ccl <= 0.0f) {
        base_ccl = safe_float(data->peak_discharge_current_limit_a);
    }
    inputs->base_ccl_limit_a = base_ccl;

    float base_dcl = safe_float(data->discharge_overcurrent_limit_a);
    if (base_dcl <= 0.0f) {
        base_dcl = safe_float(data->peak_discharge_current_limit_a);
    }
    inputs->base_dcl_limit_a = base_dcl;

    if (data->max_cell_mv > 0U) {
        inputs->max_cell_voltage_v = (float)data->max_cell_mv / 1000.0f;
    } else {
        inputs->max_cell_voltage_v = 0.0f;
    }
}

void can_publisher_cvl_prepare(const uart_bms_live_data_t *data)
{
    if (!s_cvl_initialised) {
        can_publisher_cvl_init();
    }

    if (data == NULL) {
        return;
    }

    cvl_inputs_t inputs;
    cvl_controller_prepare_inputs(data, &inputs);
    cvl_config_snapshot_t config = cvl_controller_load_config(data);

    cvl_computation_result_t result;
    cvl_compute_limits(&inputs, &config, &s_cvl_runtime, &result);

    s_cvl_runtime.state = result.state;
    s_cvl_runtime.cvl_voltage_v = result.cvl_voltage_v;
    s_cvl_runtime.cell_protection_active = result.cell_protection_active;

    s_cvl_result.timestamp_ms = data->timestamp_ms;
    s_cvl_result.result = result;
    s_cvl_has_result = true;
}

bool can_publisher_cvl_get_latest(can_publisher_cvl_result_t *out_result)
{
    if (!s_cvl_has_result || out_result == NULL) {
        return false;
    }

    *out_result = s_cvl_result;
    return true;
}

