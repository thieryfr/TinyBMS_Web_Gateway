#include "tiny_mqtt_publisher.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "app_events.h"
#include "mqtt_topics.h"

#ifndef CONFIG_TINYBMS_MQTT_ENABLE
#define CONFIG_TINYBMS_MQTT_ENABLE 0
#endif

#if CONFIG_TINYBMS_MQTT_ENABLE
#include "esp_err.h"
#include "esp_log.h"
#include "uart_bms.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif
#else
#ifndef ESP_LOGD
#define ESP_LOGD(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGI(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGW(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGE(tag, fmt, ...) (void)(tag), (void)(fmt)
#endif
#endif

#define TINY_MQTT_DEFAULT_INTERVAL_MS 1000U
#define TINY_MQTT_PAYLOAD_CAPACITY    TINY_MQTT_MAX_PAYLOAD_SIZE

static const char *TAG = "tiny_mqtt_pub";

static event_bus_publish_fn_t s_event_publisher = NULL;
static tiny_mqtt_publisher_config_t s_config = {
    .publish_interval_ms = TINY_MQTT_DEFAULT_INTERVAL_MS,
    .qos = MQTT_TOPIC_METRICS_QOS,
    .retain = MQTT_TOPIC_METRICS_RETAIN,
};
static uint64_t s_last_publish_ms = 0;
static bool s_listener_registered = false;
static tiny_mqtt_publisher_message_t s_message = {0};
static char s_payload_buffer[TINY_MQTT_PAYLOAD_CAPACITY];

static bool tiny_mqtt_payload_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
{
    if (buffer == NULL || buffer_size == 0 || offset == NULL || fmt == NULL) {
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

static float sanitize_float(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    return value;
}

static uint16_t encode_alarm_level(bool triggered)
{
    return triggered ? 2U : 0U;
}

static float extract_limit(float preferred, float fallback)
{
    preferred = sanitize_float(preferred);
    fallback = sanitize_float(fallback);

    if (preferred > 0.0f) {
        return preferred;
    }
    if (fallback > 0.0f) {
        return fallback;
    }
    return 0.0f;
}

static uint64_t extract_timestamp_ms(const uart_bms_live_data_t *data)
{
    if (data != NULL && data->timestamp_ms > 0U) {
        return data->timestamp_ms;
    }
#if defined(ESP_PLATFORM)
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    static uint64_t s_fallback_ms = 0;
    s_fallback_ms += 1000ULL;
    return s_fallback_ms;
#endif
}

static bool should_publish(uint64_t timestamp_ms)
{
    if (s_config.publish_interval_ms == 0U) {
        return true;
    }
    if (s_last_publish_ms == 0U) {
        return true;
    }
    if (timestamp_ms < s_last_publish_ms) {
        return true;
    }
    uint64_t next_allowed = s_last_publish_ms + (uint64_t)s_config.publish_interval_ms;
    return timestamp_ms >= next_allowed;
}

static bool build_payload(const uart_bms_live_data_t *data, size_t *out_length)
{
    if (data == NULL) {
        return false;
    }

    float pack_voltage = sanitize_float(data->pack_voltage_v);
    float pack_current = sanitize_float(data->pack_current_a);
    float power_w = sanitize_float(pack_voltage * pack_current);
    float average_temp = sanitize_float(data->average_temperature_c);
    float mosfet_temp = sanitize_float(data->mosfet_temperature_c);
    float min_cell_v = (data->min_cell_mv > 0U) ? ((float)data->min_cell_mv / 1000.0f) : 0.0f;
    float max_cell_v = (data->max_cell_mv > 0U) ? ((float)data->max_cell_mv / 1000.0f) : 0.0f;
    float max_charge_limit = extract_limit(data->max_charge_current_limit_a, data->charge_overcurrent_limit_a);
    float max_discharge_limit = extract_limit(data->max_discharge_current_limit_a, data->discharge_overcurrent_limit_a);
    float charge_overcurrent = extract_limit(data->charge_overcurrent_limit_a, data->max_charge_current_limit_a);
    float discharge_overcurrent = extract_limit(data->discharge_overcurrent_limit_a, data->max_discharge_current_limit_a);

    bool high_charge = false;
    if (charge_overcurrent > 0.0f && pack_current > 0.0f) {
        high_charge = pack_current >= charge_overcurrent;
    }

    bool high_discharge = false;
    if (discharge_overcurrent > 0.0f && pack_current < 0.0f) {
        high_discharge = fabsf(pack_current) >= discharge_overcurrent;
    }

    bool imbalance = data->balancing_bits != 0U;

    uint64_t timestamp_ms = extract_timestamp_ms(data);

    size_t offset = 0;
    if (!tiny_mqtt_payload_append(s_payload_buffer,
                                  sizeof(s_payload_buffer),
                                  &offset,
                                  "{\"type\":\"tinybms_metrics\",\"timestamp_ms\":%" PRIu64 ",\"uptime_s\":%" PRIu32 ",\"cycle_count\":%" PRIu32 ","
                                  "\"pack_voltage_v\":%.3f,\"pack_current_a\":%.3f,\"power_w\":%.3f,\"state_of_charge_pct\":%.2f,"
                                  "\"state_of_health_pct\":%.2f,\"average_temperature_c\":%.2f,\"mosfet_temperature_c\":%.2f,"
                                  "\"min_cell_voltage_v\":%.3f,\"max_cell_voltage_v\":%.3f,\"balancing_bits\":%u,",
                                  timestamp_ms,
                                  data->uptime_seconds,
                                  data->cycle_count,
                                  pack_voltage,
                                  pack_current,
                                  power_w,
                                  sanitize_float(data->state_of_charge_pct),
                                  sanitize_float(data->state_of_health_pct),
                                  average_temp,
                                  mosfet_temp,
                                  min_cell_v,
                                  max_cell_v,
                                  (unsigned)data->balancing_bits)) {
        return false;
    }

    if (!tiny_mqtt_payload_append(s_payload_buffer, sizeof(s_payload_buffer), &offset, "\"cell_voltages_mv\":[")) {
        return false;
    }

    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        unsigned value = (unsigned)data->cell_voltage_mv[i];
        if (!tiny_mqtt_payload_append(s_payload_buffer,
                                      sizeof(s_payload_buffer),
                                      &offset,
                                      "%s%u",
                                      (i == 0) ? "" : ",",
                                      value)) {
            return false;
        }
    }

    if (!tiny_mqtt_payload_append(s_payload_buffer, sizeof(s_payload_buffer), &offset, "],\"cell_balancing\":[")) {
        return false;
    }

    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        unsigned value = (data->cell_balancing[i] != 0U) ? 1U : 0U;
        if (!tiny_mqtt_payload_append(s_payload_buffer,
                                      sizeof(s_payload_buffer),
                                      &offset,
                                      "%s%u",
                                      (i == 0) ? "" : ",",
                                      value)) {
            return false;
        }
    }

    if (!tiny_mqtt_payload_append(s_payload_buffer,
                                  sizeof(s_payload_buffer),
                                  &offset,
                                  "],\"alarms\":{\"high_charge\":%u,\"high_discharge\":%u,\"cell_imbalance\":%u,\"raw_alarm_bits\":%u,\"raw_warning_bits\":%u},"
                                  "\"limits\":{\"max_charge_current_a\":%.2f,\"max_discharge_current_a\":%.2f,\"charge_overcurrent_limit_a\":%.2f,\"discharge_overcurrent_limit_a\":%.2f}}",
                                  encode_alarm_level(high_charge),
                                  encode_alarm_level(high_discharge),
                                  encode_alarm_level(imbalance),
                                  (unsigned)data->alarm_bits,
                                  (unsigned)data->warning_bits,
                                  max_charge_limit,
                                  max_discharge_limit,
                                  charge_overcurrent,
                                  discharge_overcurrent)) {
        return false;
    }

    if (out_length != NULL) {
        *out_length = offset;
    }
    return true;
}

void tiny_mqtt_publisher_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void tiny_mqtt_publisher_reset(void)
{
    s_last_publish_ms = 0U;
    memset(s_payload_buffer, 0, sizeof(s_payload_buffer));
    s_message.payload = s_payload_buffer;
    s_message.payload_length = 0U;
    s_message.qos = s_config.qos;
    s_message.retain = s_config.retain;
}

void tiny_mqtt_publisher_apply_config(const tiny_mqtt_publisher_config_t *config)
{
    tiny_mqtt_publisher_config_t effective = {
        .publish_interval_ms = TINY_MQTT_DEFAULT_INTERVAL_MS,
        .qos = MQTT_TOPIC_METRICS_QOS,
        .retain = MQTT_TOPIC_METRICS_RETAIN,
    };

    if (config != NULL) {
        if (config->publish_interval_ms == 0U) {
            effective.publish_interval_ms = 0U;
        } else if (config->publish_interval_ms == TINY_MQTT_PUBLISH_INTERVAL_KEEP) {
            effective.publish_interval_ms = s_config.publish_interval_ms;
        } else {
            effective.publish_interval_ms = config->publish_interval_ms;
        }
        if (config->qos < 0) {
            effective.qos = 0;
        } else if (config->qos > 2) {
            effective.qos = 2;
        } else {
            effective.qos = config->qos;
        }
        effective.retain = config->retain;
    }

    bool keep_interval = (config != NULL && config->publish_interval_ms == TINY_MQTT_PUBLISH_INTERVAL_KEEP);

    s_config = effective;

    if (keep_interval) {
        s_message.qos = s_config.qos;
        s_message.retain = s_config.retain;
    } else {
        tiny_mqtt_publisher_reset();
    }
}

void tiny_mqtt_publisher_init(const tiny_mqtt_publisher_config_t *config)
{
#if CONFIG_TINYBMS_MQTT_ENABLE
    tiny_mqtt_publisher_apply_config(config);
    if (!s_listener_registered) {
        esp_err_t err = uart_bms_register_listener(tiny_mqtt_publisher_on_bms_update, NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(err));
        } else {
            s_listener_registered = true;
        }
    }
#else
    (void)config;
    tiny_mqtt_publisher_apply_config(config);
#endif
}

void tiny_mqtt_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;

    if (data == NULL) {
        return;
    }

    uint64_t timestamp_ms = extract_timestamp_ms(data);
    if (!should_publish(timestamp_ms)) {
        return;
    }

    size_t payload_length = 0U;
    if (!build_payload(data, &payload_length)) {
        return;
    }

    s_last_publish_ms = timestamp_ms;
    s_message.payload_length = payload_length;
    s_message.qos = s_config.qos;
    s_message.retain = s_config.retain;

    if (s_event_publisher == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_MQTT_METRICS,
        .payload = &s_message,
        .payload_size = sizeof(s_message),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Unable to publish TinyBMS MQTT metrics event");
    }
}

