#include "telemetry_json.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "can_publisher.h"
#include "uart_bms.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

#define TELEMETRY_JSON_HISTORY_TYPE "history_sample"
#define TELEMETRY_JSON_METRICS_TYPE "tinybms_metrics"
#define TELEMETRY_JSON_CAN_READY_TYPE "can_ready"

static float telemetry_json_sanitize_float(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    return value;
}

static float telemetry_json_extract_limit(float preferred, float fallback)
{
    preferred = telemetry_json_sanitize_float(preferred);
    fallback = telemetry_json_sanitize_float(fallback);

    if (preferred > 0.0f) {
        return preferred;
    }
    if (fallback > 0.0f) {
        return fallback;
    }
    return 0.0f;
}

static uint16_t telemetry_json_encode_alarm_level(bool triggered)
{
    return triggered ? 2U : 0U;
}

static uint64_t telemetry_json_current_time_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    static uint64_t s_monotonic_ms = 0;
    s_monotonic_ms += 1000ULL;
    return s_monotonic_ms;
#endif
}

static uint64_t telemetry_json_extract_timestamp_ms(const uart_bms_live_data_t *data)
{
    if (data != NULL && data->timestamp_ms > 0U) {
        return data->timestamp_ms;
    }
    return telemetry_json_current_time_ms();
}

static bool telemetry_json_print(const cJSON *object, char *buffer, size_t buffer_size, size_t *out_length)
{
    if (object == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    if (cJSON_PrintPreallocated((cJSON *)object, buffer, (int)buffer_size, false)) {
        if (out_length != NULL) {
            *out_length = strlen(buffer);
        }
        return true;
    }

    char *json = cJSON_PrintUnformatted(object);
    if (json == NULL) {
        return false;
    }

    size_t length = strlen(json);
    if (length >= buffer_size) {
        cJSON_free(json);
        return false;
    }

    memcpy(buffer, json, length + 1U);
    if (out_length != NULL) {
        *out_length = length;
    }

    cJSON_free(json);
    return true;
}

static bool telemetry_json_populate_metrics(const uart_bms_live_data_t *data, cJSON *root)
{
    if (data == NULL || root == NULL) {
        return false;
    }

    float pack_voltage = telemetry_json_sanitize_float(data->pack_voltage_v);
    float pack_current = telemetry_json_sanitize_float(data->pack_current_a);
    float power_w = telemetry_json_sanitize_float(pack_voltage * pack_current);
    float average_temp = telemetry_json_sanitize_float(data->average_temperature_c);
    float mosfet_temp = telemetry_json_sanitize_float(data->mosfet_temperature_c);
    float min_cell_v = (data->min_cell_mv > 0U) ? ((float)data->min_cell_mv / 1000.0f) : 0.0f;
    float max_cell_v = (data->max_cell_mv > 0U) ? ((float)data->max_cell_mv / 1000.0f) : 0.0f;
    float max_charge_limit = telemetry_json_extract_limit(data->max_charge_current_limit_a,
                                                          data->charge_overcurrent_limit_a);
    float max_discharge_limit = telemetry_json_extract_limit(data->max_discharge_current_limit_a,
                                                             data->discharge_overcurrent_limit_a);
    float charge_overcurrent = telemetry_json_extract_limit(data->charge_overcurrent_limit_a,
                                                            data->max_charge_current_limit_a);
    float discharge_overcurrent = telemetry_json_extract_limit(data->discharge_overcurrent_limit_a,
                                                               data->max_discharge_current_limit_a);

    bool high_charge = false;
    if (charge_overcurrent > 0.0f && pack_current > 0.0f) {
        high_charge = pack_current >= charge_overcurrent;
    }

    bool high_discharge = false;
    if (discharge_overcurrent > 0.0f && pack_current < 0.0f) {
        high_discharge = fabsf(pack_current) >= discharge_overcurrent;
    }

    bool imbalance = data->balancing_bits != 0U;

    if (cJSON_AddStringToObject(root, "type", TELEMETRY_JSON_METRICS_TYPE) == NULL ||
        cJSON_AddNumberToObject(root, "timestamp_ms", (double)telemetry_json_extract_timestamp_ms(data)) == NULL ||
        cJSON_AddNumberToObject(root, "uptime_s", (double)data->uptime_seconds) == NULL ||
        cJSON_AddNumberToObject(root, "cycle_count", (double)data->cycle_count) == NULL ||
        cJSON_AddNumberToObject(root, "pack_voltage_v", pack_voltage) == NULL ||
        cJSON_AddNumberToObject(root, "pack_current_a", pack_current) == NULL ||
        cJSON_AddNumberToObject(root, "power_w", power_w) == NULL ||
        cJSON_AddNumberToObject(root, "state_of_charge_pct",
                                 telemetry_json_sanitize_float(data->state_of_charge_pct)) == NULL ||
        cJSON_AddNumberToObject(root, "state_of_health_pct",
                                 telemetry_json_sanitize_float(data->state_of_health_pct)) == NULL ||
        cJSON_AddNumberToObject(root, "average_temperature_c", average_temp) == NULL ||
        cJSON_AddNumberToObject(root, "mosfet_temperature_c", mosfet_temp) == NULL ||
        cJSON_AddNumberToObject(root, "min_cell_voltage_v", min_cell_v) == NULL ||
        cJSON_AddNumberToObject(root, "max_cell_voltage_v", max_cell_v) == NULL ||
        cJSON_AddNumberToObject(root, "balancing_bits", (double)data->balancing_bits) == NULL) {
        return false;
    }

    cJSON *cell_voltages = cJSON_AddArrayToObject(root, "cell_voltages_mv");
    if (cell_voltages == NULL) {
        return false;
    }
    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        cJSON *entry = cJSON_CreateNumber((double)data->cell_voltage_mv[i]);
        if (entry == NULL) {
            return false;
        }
        cJSON_AddItemToArray(cell_voltages, entry);
    }

    cJSON *cell_balancing = cJSON_AddArrayToObject(root, "cell_balancing");
    if (cell_balancing == NULL) {
        return false;
    }
    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        cJSON *entry = cJSON_CreateNumber((double)((data->cell_balancing[i] != 0U) ? 1U : 0U));
        if (entry == NULL) {
            return false;
        }
        cJSON_AddItemToArray(cell_balancing, entry);
    }

    cJSON *alarms = cJSON_AddObjectToObject(root, "alarms");
    if (alarms == NULL) {
        return false;
    }
    if (cJSON_AddNumberToObject(alarms, "high_charge", telemetry_json_encode_alarm_level(high_charge)) == NULL ||
        cJSON_AddNumberToObject(alarms, "high_discharge", telemetry_json_encode_alarm_level(high_discharge)) == NULL ||
        cJSON_AddNumberToObject(alarms, "cell_imbalance", telemetry_json_encode_alarm_level(imbalance)) == NULL ||
        cJSON_AddNumberToObject(alarms, "raw_alarm_bits", (double)data->alarm_bits) == NULL ||
        cJSON_AddNumberToObject(alarms, "raw_warning_bits", (double)data->warning_bits) == NULL) {
        return false;
    }

    cJSON *limits = cJSON_AddObjectToObject(root, "limits");
    if (limits == NULL) {
        return false;
    }
    if (cJSON_AddNumberToObject(limits, "max_charge_current_a", max_charge_limit) == NULL ||
        cJSON_AddNumberToObject(limits, "max_discharge_current_a", max_discharge_limit) == NULL ||
        cJSON_AddNumberToObject(limits, "charge_overcurrent_limit_a", charge_overcurrent) == NULL ||
        cJSON_AddNumberToObject(limits, "discharge_overcurrent_limit_a", discharge_overcurrent) == NULL) {
        return false;
    }

    return true;
}

bool telemetry_json_write_metrics(const uart_bms_live_data_t *data,
                                  char *buffer,
                                  size_t buffer_size,
                                  size_t *out_length)
{
    if (data == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }

    bool ok = telemetry_json_populate_metrics(data, root) &&
              telemetry_json_print(root, buffer, buffer_size, out_length);

    cJSON_Delete(root);
    return ok;
}

static bool telemetry_json_populate_can_ready(const can_publisher_frame_t *frame, cJSON *root)
{
    if (frame == NULL || root == NULL) {
        return false;
    }

    char id_buffer[9];
    snprintf(id_buffer, sizeof(id_buffer), "%08" PRIX32, frame->id);

    char data_buffer[(sizeof(frame->data) * 2) + 1];
    memset(data_buffer, 0, sizeof(data_buffer));
    for (size_t i = 0; i < frame->dlc && i < sizeof(frame->data); ++i) {
        snprintf(&data_buffer[i * 2], sizeof(data_buffer) - (i * 2), "%02X", frame->data[i]);
    }

    if (cJSON_AddStringToObject(root, "type", TELEMETRY_JSON_CAN_READY_TYPE) == NULL ||
        cJSON_AddStringToObject(root, "id", id_buffer) == NULL ||
        cJSON_AddNumberToObject(root, "timestamp", (double)frame->timestamp_ms) == NULL ||
        cJSON_AddNumberToObject(root, "dlc", (double)frame->dlc) == NULL ||
        cJSON_AddStringToObject(root, "data", data_buffer) == NULL) {
        return false;
    }

    return true;
}

bool telemetry_json_write_can_ready(const can_publisher_frame_t *frame,
                                    char *buffer,
                                    size_t buffer_size,
                                    size_t *out_length)
{
    if (frame == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }

    bool ok = telemetry_json_populate_can_ready(frame, root) &&
              telemetry_json_print(root, buffer, buffer_size, out_length);

    cJSON_Delete(root);
    return ok;
}

static void telemetry_json_format_iso(time_t now, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0U) {
        return;
    }

    if (now <= 0) {
        snprintf(buffer, size, "1970-01-01T00:00:00Z");
        return;
    }

    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_now);
}

static bool telemetry_json_populate_history(const uart_bms_live_data_t *sample,
                                            time_t now,
                                            cJSON *root)
{
    if (sample == NULL || root == NULL) {
        return false;
    }

    char iso[32];
    telemetry_json_format_iso(now, iso, sizeof(iso));

    if (cJSON_AddStringToObject(root, "type", TELEMETRY_JSON_HISTORY_TYPE) == NULL ||
        cJSON_AddStringToObject(root, "timestamp_iso", iso) == NULL ||
        cJSON_AddNumberToObject(root, "timestamp_ms", (double)sample->timestamp_ms) == NULL ||
        cJSON_AddNumberToObject(root, "pack_voltage_v",
                                 telemetry_json_sanitize_float(sample->pack_voltage_v)) == NULL ||
        cJSON_AddNumberToObject(root, "pack_current_a",
                                 telemetry_json_sanitize_float(sample->pack_current_a)) == NULL ||
        cJSON_AddNumberToObject(root, "state_of_charge_pct",
                                 telemetry_json_sanitize_float(sample->state_of_charge_pct)) == NULL ||
        cJSON_AddNumberToObject(root, "state_of_health_pct",
                                 telemetry_json_sanitize_float(sample->state_of_health_pct)) == NULL ||
        cJSON_AddNumberToObject(root, "average_temperature_c",
                                 telemetry_json_sanitize_float(sample->average_temperature_c)) == NULL) {
        return false;
    }

    return true;
}

bool telemetry_json_write_history_sample(const uart_bms_live_data_t *sample,
                                         time_t now,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length)
{
    if (sample == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }

    bool ok = telemetry_json_populate_history(sample, now, root) &&
              telemetry_json_print(root, buffer, buffer_size, out_length);

    cJSON_Delete(root);
    return ok;
}

