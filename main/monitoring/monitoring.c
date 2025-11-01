#include "monitoring.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"

#include "app_events.h"
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
static char s_last_snapshot[1024] = {0};
static size_t s_last_snapshot_len = 0;

static bool monitoring_history_empty(void)
{
    return s_history_count == 0;
}

static void monitoring_history_push(const uart_bms_live_data_t *data)
{
    if (data == NULL) {
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

    size_t offset = 0;
    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "{\"type\":\"battery\",\"timestamp\":%" PRIu64 ","
                                "\"pack_voltage\":%.3f,\"pack_current\":%.3f,\"min_cell_mv\":%u,"
                                "\"max_cell_mv\":%u,\"state_of_charge\":%.2f,\"state_of_health\":%.2f,"
                                "\"average_temperature\":%.2f,\"mos_temperature\":%.2f,"
                                "\"balancing_bits\":%u,\"alarm_bits\":%u,\"warning_bits\":%u,"
                                "\"uptime_seconds\":%" PRIu32 ",\"cycle_count\":%" PRIu32 ",\"registers\":[",
                                snapshot->timestamp_ms,
                                snapshot->pack_voltage_v,
                                snapshot->pack_current_a,
                                (unsigned)snapshot->min_cell_mv,
                                (unsigned)snapshot->max_cell_mv,
                                snapshot->state_of_charge_pct,
                                snapshot->state_of_health_pct,
                                snapshot->average_temperature_c,
                                snapshot->mosfet_temperature_c,
                                (unsigned)snapshot->balancing_bits,
                                (unsigned)snapshot->alarm_bits,
                                (unsigned)snapshot->warning_bits,
                                snapshot->uptime_seconds,
                                snapshot->cycle_count)) {
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

    if (!monitoring_json_append(buffer, buffer_size, &offset, "],\"history_available\":%s}",
                                monitoring_history_empty() ? "false" : "true")) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}

static esp_err_t monitoring_prepare_snapshot(void)
{
    const uart_bms_live_data_t *snapshot = s_has_latest_bms ? &s_latest_bms : NULL;
    return monitoring_build_snapshot_json(snapshot, s_last_snapshot, sizeof(s_last_snapshot), &s_last_snapshot_len);
}

static void monitoring_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) {
        return;
    }

    s_latest_bms = *data;
    s_has_latest_bms = true;
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
}

esp_err_t monitoring_get_status_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_bms_live_data_t *snapshot = s_has_latest_bms ? &s_latest_bms : NULL;
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
        ESP_LOGW(TAG, "Unable to publish telemetry snapshot");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t monitoring_get_history_json(size_t limit, char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t available = s_history_count;
    if (available == 0) {
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
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "]}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }

    return ESP_OK;
}
