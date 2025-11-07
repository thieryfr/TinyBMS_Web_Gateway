#include "monitoring.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
static char s_last_snapshot[MONITORING_SNAPSHOT_MAX_SIZE] = {0};
static size_t s_last_snapshot_len = 0;

// Mutex to protect access to shared monitoring state
static SemaphoreHandle_t s_monitoring_mutex = NULL;

static bool monitoring_history_empty(void)
{
    if (s_monitoring_mutex == NULL) {
        return true;
    }

    bool empty = false;
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        empty = (s_history_count == 0);
        xSemaphoreGive(s_monitoring_mutex);
    }
    return empty;
}

static void monitoring_history_push(const uart_bms_live_data_t *data)
{
    if (data == NULL || s_monitoring_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for history push");
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

    size_t offset = 0;
    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "{\"type\":\"battery\",\"timestamp\":%" PRIu64 ","
                                "\"pack_voltage\":%.3f,\"pack_current\":%.3f,\"min_cell_mv\":%u,"
                                "\"max_cell_mv\":%u,\"state_of_charge\":%.2f,\"state_of_health\":%.2f,"
                                "\"average_temperature\":%.2f,\"mos_temperature\":%.2f,"
                                "\"balancing_bits\":%u,",
                                snapshot->timestamp_ms,
                                snapshot->pack_voltage_v,
                                snapshot->pack_current_a,
                                (unsigned)snapshot->min_cell_mv,
                                (unsigned)snapshot->max_cell_mv,
                                snapshot->state_of_charge_pct,
                                snapshot->state_of_health_pct,
                                snapshot->average_temperature_c,
                                snapshot->mosfet_temperature_c,
                                (unsigned)snapshot->balancing_bits)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!monitoring_json_append(buffer, buffer_size, &offset, "\"cell_voltages_mv\":[")) {
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

    if (!monitoring_json_append(buffer,
                                buffer_size,
                                &offset,
                                "],\"alarm_bits\":%u,\"warning_bits\":%u,"
                                "\"uptime_seconds\":%" PRIu32 ",\"estimated_time_left_seconds\":%" PRIu32 ",\"cycle_count\":%" PRIu32 ",\"registers\":[",
                                (unsigned)snapshot->alarm_bits,
                                (unsigned)snapshot->warning_bits,
                                snapshot->uptime_seconds,
                                snapshot->estimated_time_left_seconds,
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
        ESP_LOGW(TAG, "Failed to acquire mutex for snapshot preparation");
        return ESP_ERR_TIMEOUT;
    }

    const uart_bms_live_data_t *snapshot = has_data ? &bms_copy : NULL;
    return monitoring_build_snapshot_json(snapshot, s_last_snapshot, sizeof(s_last_snapshot), &s_last_snapshot_len);
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
        ESP_LOGW(TAG, "Failed to acquire mutex for BMS update");
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

    if (s_monitoring_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire mutex for reading history data
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for history read");
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
