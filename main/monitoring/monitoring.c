#include "monitoring.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

#include "app_events.h"
#include "can_publisher/conversion_table.h"
#include "uart_bms.h"
#include "history_logger.h"

static const char *TAG = "monitoring";

typedef struct {
    uint64_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    float state_of_charge_pct;
    float state_of_health_pct;
    float average_temperature_c;
} monitoring_history_entry_t;

#define MONITORING_HISTORY_CAPACITY 512

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_live_data_t s_latest_bms = {0};
static bool s_has_latest_bms = false;
static monitoring_history_entry_t s_history[MONITORING_HISTORY_CAPACITY];
static size_t s_history_head = 0;
static size_t s_history_count = 0;
static char s_last_snapshot[MONITORING_SNAPSHOT_MAX_SIZE] = {0};
static size_t s_last_snapshot_len = 0;

// Mutex to protect access to shared monitoring state
static SemaphoreHandle_t s_monitoring_mutex = NULL;

#define MONITORING_DIAGNOSTICS_INTERVAL_MS    5000U
#define MONITORING_MAX_EVENT_BUS_CONSUMERS    16U

typedef struct {
    uint32_t mutex_timeouts;
    uint32_t queue_publish_failures;
    uint64_t last_queue_failure_ms;
    uint64_t snapshot_latency_total_us;
    uint32_t snapshot_latency_samples;
    uint32_t snapshot_latency_max_us;
} monitoring_diagnostics_state_t;

static monitoring_diagnostics_state_t s_diagnostics_state = {0};
static portMUX_TYPE s_diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_last_diagnostics[MONITORING_DIAGNOSTICS_MAX_SIZE] = {0};
static size_t s_last_diagnostics_len = 0;
static esp_timer_handle_t s_diagnostics_timer = NULL;

static uint32_t monitoring_diagnostics_record_mutex_timeout(void)
{
    uint32_t value = 0;
    portENTER_CRITICAL(&s_diagnostics_lock);
    s_diagnostics_state.mutex_timeouts++;
    value = s_diagnostics_state.mutex_timeouts;
    portEXIT_CRITICAL(&s_diagnostics_lock);
    return value;
}

static uint32_t monitoring_diagnostics_record_publish_failure(void)
{
    uint32_t value = 0;
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    portENTER_CRITICAL(&s_diagnostics_lock);
    s_diagnostics_state.queue_publish_failures++;
    s_diagnostics_state.last_queue_failure_ms = now_ms;
    value = s_diagnostics_state.queue_publish_failures;
    portEXIT_CRITICAL(&s_diagnostics_lock);
    return value;
}

static void monitoring_diagnostics_record_snapshot_latency(uint32_t duration_us)
{
    portENTER_CRITICAL(&s_diagnostics_lock);
    s_diagnostics_state.snapshot_latency_total_us += (uint64_t)duration_us;
    s_diagnostics_state.snapshot_latency_samples++;
    if (duration_us > s_diagnostics_state.snapshot_latency_max_us) {
        s_diagnostics_state.snapshot_latency_max_us = duration_us;
    }
    portEXIT_CRITICAL(&s_diagnostics_lock);
}

static void monitoring_diagnostics_get_snapshot(monitoring_diagnostics_state_t *out_state)
{
    if (out_state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_diagnostics_lock);
    *out_state = s_diagnostics_state;
    portEXIT_CRITICAL(&s_diagnostics_lock);
}

static void monitoring_diagnostics_reset(void)
{
    portENTER_CRITICAL(&s_diagnostics_lock);
    memset(&s_diagnostics_state, 0, sizeof(s_diagnostics_state));
    portEXIT_CRITICAL(&s_diagnostics_lock);
}

static void monitoring_diagnostics_timer_callback(void *arg)
{
    (void)arg;

    if (s_event_publisher == NULL) {
        return;
    }

    esp_err_t err = monitoring_publish_diagnostics_snapshot();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "Failed to publish monitoring diagnostics: %s",
                 esp_err_to_name(err));
    }
}

static bool monitoring_history_empty(void)
{
    if (s_monitoring_mutex == NULL) {
        return true;
    }

    bool empty = false;
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        empty = (s_history_count == 0);
        xSemaphoreGive(s_monitoring_mutex);
    } else {
        (void)monitoring_diagnostics_record_mutex_timeout();
    }
    return empty;
}

static void monitoring_history_push(const uart_bms_live_data_t *data)
{
    if (data == NULL || s_monitoring_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        uint32_t count = monitoring_diagnostics_record_mutex_timeout();
        ESP_LOGW(TAG,
                 "Failed to acquire mutex for history push (timeout #%u)",
                 (unsigned)count);
        return;
    }

    monitoring_history_entry_t *slot = &s_history[s_history_head];
    slot->timestamp_ms = data->timestamp_ms;
    slot->pack_voltage_v = data->pack_voltage_v;
    slot->pack_current_a = data->pack_current_a;
    slot->state_of_charge_pct = data->state_of_charge_pct;
    slot->state_of_health_pct = data->state_of_health_pct;
    slot->average_temperature_c = data->average_temperature_c;

    s_history_head = (s_history_head + 1) % MONITORING_HISTORY_CAPACITY;
    if (s_history_count < MONITORING_HISTORY_CAPACITY) {
        ++s_history_count;
    }

    xSemaphoreGive(s_monitoring_mutex);
}

static bool monitoring_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
{
    if (buffer == NULL || buffer_size == 0 || offset == NULL) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *offset, buffer_size - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    size_t remaining = buffer_size - *offset;
    if ((size_t)written >= remaining) {
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static esp_err_t monitoring_build_snapshot_json(const uart_bms_live_data_t *data,
                                                char *buffer,
                                                size_t buffer_size,
                                                size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_bms_live_data_t empty = {0};
    const uart_bms_live_data_t *snapshot = data != NULL ? data : &empty;

    double energy_charged_wh = 0.0;
    double energy_discharged_wh = 0.0;
    can_publisher_conversion_get_energy_state(&energy_charged_wh, &energy_discharged_wh);

    if (!isfinite(energy_charged_wh) || energy_charged_wh < 0.0) {
        energy_charged_wh = 0.0;
    }
    if (!isfinite(energy_discharged_wh) || energy_discharged_wh < 0.0) {
        energy_discharged_wh = 0.0;
    }

    float pack_voltage_v = snapshot->pack_voltage_v;
    if (!isfinite((double)pack_voltage_v)) {
        pack_voltage_v = 0.0f;
    }

    float pack_current_a = snapshot->pack_current_a;
    if (!isfinite((double)pack_current_a)) {
        pack_current_a = 0.0f;
    }

    float power_w = pack_voltage_v * pack_current_a;
    if (!isfinite((double)power_w)) {
        power_w = 0.0f;
    }

    bool is_charging = (pack_current_a > 0.05f);

    uint32_t energy_in_wh = (energy_charged_wh > 0.0) ? (uint32_t)(energy_charged_wh + 0.5) : 0U;
    uint32_t energy_out_wh = (energy_discharged_wh > 0.0) ? (uint32_t)(energy_discharged_wh + 0.5) : 0U;

    size_t offset = 0;
    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "{\"type\":\"battery\",\"timestamp_ms\":%" PRIu64 ","
                                "\"pack_voltage_v\":%.3f,\"pack_current_a\":%.3f,\"power_w\":%.3f,"
                                "\"is_charging\":%s,\"state_of_charge_pct\":%.2f,\"state_of_health_pct\":%.2f,"
                                "\"average_temperature_c\":%.2f,\"mos_temperature_c\":%.2f,\"auxiliary_temperature_c\":%.2f,"
                                "\"pack_temperature_min_c\":%.2f,\"pack_temperature_max_c\":%.2f,"
                                "\"min_cell_mv\":%u,\"max_cell_mv\":%u,\"balancing_bits\":%u,"
                                "\"alarm_bits\":%u,\"warning_bits\":%u,"
                                "\"uptime_seconds\":%" PRIu32 ",\"estimated_time_left_seconds\":%" PRIu32 ",\"cycle_count\":%" PRIu32 ","
                                "\"battery_capacity_ah\":%.2f,\"series_cell_count\":%u,"
                                "\"overvoltage_cutoff_mv\":%u,\"undervoltage_cutoff_mv\":%u,"
                                "\"discharge_overcurrent_limit_a\":%.3f,\"charge_overcurrent_limit_a\":%.3f,"
                                "\"max_discharge_current_limit_a\":%.3f,\"max_charge_current_limit_a\":%.3f,"
                                "\"peak_discharge_current_limit_a\":%.3f,\"overheat_cutoff_c\":%.2f,\"low_temp_charge_cutoff_c\":%.2f,"
                                "\"hardware_version\":%u,\"hardware_changes_version\":%u,\"firmware_version\":%u,"
                                "\"firmware_flags\":%u,\"internal_firmware_version\":%u,"
                                "\"energy_charged_wh\":%u,\"energy_discharged_wh\":%u,",
                                snapshot->timestamp_ms,
                                pack_voltage_v,
                                pack_current_a,
                                power_w,
                                is_charging ? "true" : "false",
                                snapshot->state_of_charge_pct,
                                snapshot->state_of_health_pct,
                                snapshot->average_temperature_c,
                                snapshot->mosfet_temperature_c,
                                snapshot->auxiliary_temperature_c,
                                snapshot->pack_temperature_min_c,
                                snapshot->pack_temperature_max_c,
                                (unsigned)snapshot->min_cell_mv,
                                (unsigned)snapshot->max_cell_mv,
                                (unsigned)snapshot->balancing_bits,
                                (unsigned)snapshot->alarm_bits,
                                (unsigned)snapshot->warning_bits,
                                snapshot->uptime_seconds,
                                snapshot->estimated_time_left_seconds,
                                snapshot->cycle_count,
                                snapshot->battery_capacity_ah,
                                (unsigned)snapshot->series_cell_count,
                                (unsigned)snapshot->overvoltage_cutoff_mv,
                                (unsigned)snapshot->undervoltage_cutoff_mv,
                                snapshot->discharge_overcurrent_limit_a,
                                snapshot->charge_overcurrent_limit_a,
                                snapshot->max_discharge_current_limit_a,
                                snapshot->max_charge_current_limit_a,
                                snapshot->peak_discharge_current_limit_a,
                                snapshot->overheat_cutoff_c,
                                snapshot->low_temp_charge_cutoff_c,
                                (unsigned)snapshot->hardware_version,
                                (unsigned)snapshot->hardware_changes_version,
                                (unsigned)snapshot->firmware_version,
                                (unsigned)snapshot->firmware_flags,
                                (unsigned)snapshot->internal_firmware_version,
                                (unsigned)energy_in_wh,
                                (unsigned)energy_out_wh)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "\"cell_voltage_mv\":[")) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        unsigned value = (unsigned)snapshot->cell_voltage_mv[i];
        if (!monitoring_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "%s%u",
                                    (i == 0) ? "" : ",",
                                    value)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "],\"cell_balancing\":[")) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        unsigned value = (snapshot->cell_balancing[i] != 0U) ? 1U : 0U;
        if (!monitoring_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "%s%u",
                                    (i == 0) ? "" : ",",
                                    value)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "],\"registers\":[")) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < snapshot->register_count; ++i) {
        const uart_bms_register_entry_t *entry = &snapshot->registers[i];
        if (!monitoring_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "%s{\"address\":%u,\"value\":%u}",
                                    (i == 0) ? "" : ",",
                                    (unsigned)entry->address,
                                    (unsigned)entry->raw_value)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "],\"history_available\":%s}",
                                monitoring_history_empty() ? "false" : "true")) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}

static esp_err_t monitoring_build_diagnostics_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    monitoring_diagnostics_state_t diagnostics = {0};
    monitoring_diagnostics_get_snapshot(&diagnostics);

    uint32_t avg_latency_us = 0;
    if (diagnostics.snapshot_latency_samples > 0U && diagnostics.snapshot_latency_total_us > 0U) {
        avg_latency_us =
            (uint32_t)(diagnostics.snapshot_latency_total_us / diagnostics.snapshot_latency_samples);
    }

    event_bus_subscription_metrics_t bus_metrics[MONITORING_MAX_EVENT_BUS_CONSUMERS];
    size_t consumer_count = event_bus_get_all_metrics(bus_metrics, MONITORING_MAX_EVENT_BUS_CONSUMERS);
    uint32_t dropped_total = 0U;
    for (size_t i = 0; i < consumer_count && i < MONITORING_MAX_EVENT_BUS_CONSUMERS; ++i) {
        dropped_total += bus_metrics[i].dropped_events;
    }

    uint64_t timestamp_ms = esp_timer_get_time() / 1000ULL;

    size_t offset = 0U;
    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "{\"type\":\"monitoring_diagnostics\",\"timestamp_ms\":%" PRIu64 ",",
                                timestamp_ms)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "\"mutex_timeouts\":%" PRIu32 ",",
                                diagnostics.mutex_timeouts)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "\"queue_saturation\":{\"publish_failures\":%" PRIu32 ",\"last_failure_ms\":%" PRIu64 ","
                                "\"dropped_events_total\":%" PRIu32 ",\"consumer_count\":%zu},",
                                diagnostics.queue_publish_failures,
                                diagnostics.last_queue_failure_ms,
                                dropped_total,
                                consumer_count)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "\"snapshot_latency\":{\"avg_us\":%" PRIu32 ",\"max_us\":%" PRIu32 ",\"samples\":%" PRIu32 "}}",
                                avg_latency_us,
                                diagnostics.snapshot_latency_max_us,
                                diagnostics.snapshot_latency_samples)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}

static esp_err_t monitoring_prepare_snapshot(void)
{
    if (s_monitoring_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Copy BMS data under mutex protection
    uart_bms_live_data_t bms_copy = {0};
    bool has_data = false;

    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_has_latest_bms) {
            bms_copy = s_latest_bms;
            has_data = true;
        }
        xSemaphoreGive(s_monitoring_mutex);
    } else {
        uint32_t count = monitoring_diagnostics_record_mutex_timeout();
        ESP_LOGW(TAG,
                 "Failed to acquire mutex for snapshot preparation (timeout #%u)",
                 (unsigned)count);
        return ESP_ERR_TIMEOUT;
    }

    const uart_bms_live_data_t *snapshot = has_data ? &bms_copy : NULL;
    uint64_t start_us = esp_timer_get_time();
    esp_err_t build_err =
        monitoring_build_snapshot_json(snapshot, s_last_snapshot, sizeof(s_last_snapshot), &s_last_snapshot_len);
    if (build_err == ESP_OK) {
        uint64_t duration_us = esp_timer_get_time() - start_us;
        if (duration_us > UINT32_MAX) {
            duration_us = UINT32_MAX;
        }
        monitoring_diagnostics_record_snapshot_latency((uint32_t)duration_us);
    }

    return build_err;
}

static void monitoring_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL || s_monitoring_mutex == NULL) {
        return;
    }

    // Update latest BMS data with mutex protection
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_latest_bms = *data;
        s_has_latest_bms = true;
        xSemaphoreGive(s_monitoring_mutex);
    } else {
        uint32_t count = monitoring_diagnostics_record_mutex_timeout();
        ESP_LOGW(TAG,
                 "Failed to acquire mutex for BMS update (timeout #%u)",
                 (unsigned)count);
    }

    monitoring_history_push(data);
    history_logger_handle_sample(data);

    esp_err_t err = monitoring_publish_telemetry_snapshot();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to publish telemetry snapshot after TinyBMS update: %s", esp_err_to_name(err));
    }
}

void monitoring_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void monitoring_init(void)
{
    // Initialize mutex for thread-safe access to monitoring state
    s_monitoring_mutex = xSemaphoreCreateMutex();
    if (s_monitoring_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create monitoring mutex");
        return;
    }

    esp_err_t reg_err = uart_bms_register_listener(monitoring_on_bms_update, NULL);
    if (reg_err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(reg_err));
    }

    esp_err_t snapshot_err = monitoring_prepare_snapshot();
    if (snapshot_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial telemetry snapshot build failed: %s", esp_err_to_name(snapshot_err));
    }

    esp_err_t publish_err = monitoring_publish_telemetry_snapshot();
    if (publish_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial telemetry publish failed: %s", esp_err_to_name(publish_err));
    }

    if (s_diagnostics_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = monitoring_diagnostics_timer_callback,
            .name = "mon_diag",
        };
        esp_err_t timer_err = esp_timer_create(&timer_args, &s_diagnostics_timer);
        if (timer_err != ESP_OK) {
            ESP_LOGW(TAG, "Unable to create diagnostics timer: %s", esp_err_to_name(timer_err));
        } else {
            timer_err = esp_timer_start_periodic(
                s_diagnostics_timer, (uint64_t)MONITORING_DIAGNOSTICS_INTERVAL_MS * 1000ULL);
            if (timer_err != ESP_OK) {
                ESP_LOGW(TAG, "Unable to start diagnostics timer: %s", esp_err_to_name(timer_err));
            }
        }
    }

    esp_err_t diag_err = monitoring_publish_diagnostics_snapshot();
    if (diag_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial diagnostics publish failed: %s", esp_err_to_name(diag_err));
    }
}

esp_err_t monitoring_get_status_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Acquérir mutex pour lecture thread-safe
    if (s_monitoring_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uart_bms_live_data_t local_data;
    bool has_data = false;

    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        uint32_t count = monitoring_diagnostics_record_mutex_timeout();
        ESP_LOGW(TAG,
                 "Mutex timeout reading status (timeout #%u)",
                 (unsigned)count);
        // Retourner erreur au lieu d'accéder sans protection
        return ESP_ERR_TIMEOUT;
    }

    has_data = s_has_latest_bms;
    if (has_data) {
        local_data = s_latest_bms;
    }

    xSemaphoreGive(s_monitoring_mutex);

    // Construire JSON avec données locales (hors mutex)
    const uart_bms_live_data_t *snapshot = has_data ? &local_data : NULL;
    return monitoring_build_snapshot_json(snapshot, buffer, buffer_size, out_length);
}

esp_err_t monitoring_publish_telemetry_snapshot(void)
{
    if (s_event_publisher == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = monitoring_prepare_snapshot();
    if (err != ESP_OK) {
        return err;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_TELEMETRY_SAMPLE,
        .payload = s_last_snapshot,
        .payload_size = s_last_snapshot_len + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        uint32_t count = monitoring_diagnostics_record_publish_failure();
        ESP_LOGW(TAG,
                 "Unable to publish telemetry snapshot (queue saturation #%u)",
                 (unsigned)count);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t monitoring_publish_diagnostics_snapshot(void)
{
    if (s_event_publisher == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = monitoring_build_diagnostics_json(
        s_last_diagnostics, sizeof(s_last_diagnostics), &s_last_diagnostics_len);
    if (err != ESP_OK) {
        return err;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_MONITORING_DIAGNOSTICS,
        .payload = s_last_diagnostics,
        .payload_size = s_last_diagnostics_len + 1U,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(10))) {
        uint32_t count = monitoring_diagnostics_record_publish_failure();
        ESP_LOGW(TAG,
                 "Unable to publish monitoring diagnostics (queue saturation #%u)",
                 (unsigned)count);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t monitoring_get_history_json(size_t limit, char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_monitoring_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire mutex for reading history data
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        uint32_t count = monitoring_diagnostics_record_mutex_timeout();
        ESP_LOGW(TAG,
                 "Failed to acquire mutex for history read (timeout #%u)",
                 (unsigned)count);
        return ESP_ERR_TIMEOUT;
    }

    size_t available = s_history_count;
    if (available == 0) {
        xSemaphoreGive(s_monitoring_mutex);
        int written = snprintf(buffer, buffer_size, "{\"total\":0,\"samples\":[]}");
        if (written < 0 || (size_t)written >= buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (out_length != NULL) {
            *out_length = (size_t)written;
        }
        return ESP_OK;
    }

    size_t max_samples = (limit == 0 || limit > available) ? available : limit;
    size_t offset = 0;
    if (!monitoring_json_append(buffer, buffer_size, &offset, "{\"total\":%zu,\"samples\":[", available)) {
        xSemaphoreGive(s_monitoring_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t start_index = (s_history_head + MONITORING_HISTORY_CAPACITY - max_samples) % MONITORING_HISTORY_CAPACITY;
    for (size_t i = 0; i < max_samples; ++i) {
        size_t idx = (start_index + i) % MONITORING_HISTORY_CAPACITY;
        const monitoring_history_entry_t *entry = &s_history[idx];
        if (!monitoring_json_append(buffer,
                                    buffer_size,
                                    &offset,
                                    "%s{\"timestamp\":%" PRIu64 ",\"pack_voltage\":%.3f,\"pack_current\":%.3f,"
                                    "\"state_of_charge\":%.2f,\"state_of_health\":%.2f,\"average_temperature\":%.2f}",
                                    (i == 0) ? "" : ",",
                                    entry->timestamp_ms,
                                    entry->pack_voltage_v,
                                    entry->pack_current_a,
                                    entry->state_of_charge_pct,
                                    entry->state_of_health_pct,
                                    entry->average_temperature_c)) {
            xSemaphoreGive(s_monitoring_mutex);
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "]}")) {
        xSemaphoreGive(s_monitoring_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreGive(s_monitoring_mutex);

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}

void monitoring_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing monitoring...");

    // Unregister BMS listener
    esp_err_t err = uart_bms_unregister_listener(monitoring_on_bms_update);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unregister BMS listener: %s", esp_err_to_name(err));
    }

    if (s_diagnostics_timer != NULL) {
        esp_timer_stop(s_diagnostics_timer);
        esp_timer_delete(s_diagnostics_timer);
        s_diagnostics_timer = NULL;
    }

    // Destroy mutex
    if (s_monitoring_mutex != NULL) {
        vSemaphoreDelete(s_monitoring_mutex);
        s_monitoring_mutex = NULL;
    }

    // Reset state
    s_has_latest_bms = false;
    s_event_publisher = NULL;
    s_history_head = 0;
    s_history_count = 0;
    s_last_snapshot_len = 0;
    s_last_diagnostics_len = 0;
    memset(&s_latest_bms, 0, sizeof(s_latest_bms));
    memset(s_history, 0, sizeof(s_history));
    memset(s_last_snapshot, 0, sizeof(s_last_snapshot));
    memset(s_last_diagnostics, 0, sizeof(s_last_diagnostics));
    monitoring_diagnostics_reset();

    ESP_LOGI(TAG, "Monitoring deinitialized");
}
