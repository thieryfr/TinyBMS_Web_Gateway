#include "monitoring.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"

#include "app_events.h"
#include "uart_bms.h"

static const char *TAG = "monitoring";

static event_bus_publish_fn_t s_event_publisher = NULL;
static float s_pack_voltage = 52.4f;
static float s_pack_current = -1.8f;
static uint8_t s_state_of_charge = 78;
static int8_t s_temperature_c = 25;

static char s_last_snapshot[256] = {0};
static size_t s_last_snapshot_len = 0;

static void monitoring_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) {
        return;
    }

    if (data->pack_voltage_valid) {
        s_pack_voltage = data->pack_voltage_v;
    }

    if (data->pack_current_valid) {
        s_pack_current = data->pack_current_a;
    }

    if (data->state_of_charge_valid) {
        float soc = data->state_of_charge_pct;
        if (soc < 0.0f) {
            soc = 0.0f;
        } else if (soc > 100.0f) {
            soc = 100.0f;
        }
        s_state_of_charge = (uint8_t)(soc + 0.5f);
    }

    if (data->average_temperature_valid) {
        float average_temp = data->average_temperature_c;
        if (average_temp < -128.0f) {
            average_temp = -128.0f;
        } else if (average_temp > 127.0f) {
            average_temp = 127.0f;
        }
        s_temperature_c = (int8_t)(average_temp + (average_temp >= 0 ? 0.5f : -0.5f));
    }

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

    esp_err_t err = monitoring_publish_telemetry_snapshot();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial telemetry publish failed: %s", esp_err_to_name(err));
    }
}

esp_err_t monitoring_get_status_json(char *buffer, size_t buffer_size, size_t *out_length)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    int written = snprintf(buffer,
                           buffer_size,
                           "{\"uptime_ms\":%" PRIu32 ",\"pack_voltage\":%.2f,"
                           "\"pack_current\":%.2f,\"state_of_charge\":%u,\"temperature_c\":%d}",
                           uptime_ms,
                           s_pack_voltage,
                           s_pack_current,
                           s_state_of_charge,
                           (int)s_temperature_c);
    if (written < 0) {
        return ESP_FAIL;
    }

    if ((size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_length != NULL) {
        *out_length = (size_t)written;
    }

    return ESP_OK;
}

esp_err_t monitoring_publish_telemetry_snapshot(void)
{
    if (s_event_publisher == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = monitoring_get_status_json(s_last_snapshot, sizeof(s_last_snapshot), &s_last_snapshot_len);
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
