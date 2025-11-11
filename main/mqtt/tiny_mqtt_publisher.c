#include "tiny_mqtt_publisher.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "app_config.h"
#include "app_events.h"
#include "config_manager.h"
#include "mqtt_topics.h"
#include "telemetry_json.h"

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
static char s_metrics_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
static size_t s_metrics_topic_length = 0U;
static char s_payload_buffer[TINY_MQTT_PAYLOAD_CAPACITY];
static tiny_mqtt_publisher_message_t s_message = {
    .topic = s_metrics_topic,
    .payload = s_payload_buffer,
};

typedef struct {
    tiny_mqtt_publisher_message_t message;
    char payload[TINY_MQTT_PAYLOAD_CAPACITY];
} tiny_mqtt_publisher_event_buffer_t;

static void tiny_mqtt_publisher_release_event_buffer(void *context)
{
    if (context != NULL) {
        free(context);
    }
}

static uint64_t tiny_mqtt_publisher_extract_timestamp(const uart_bms_live_data_t *data)
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

static void tiny_mqtt_publisher_set_topic_internal(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') {
        (void)snprintf(s_metrics_topic, sizeof(s_metrics_topic), MQTT_TOPIC_FMT_METRICS, APP_DEVICE_NAME);
    } else {
        (void)snprintf(s_metrics_topic, sizeof(s_metrics_topic), "%s", topic);
    }

    s_metrics_topic[sizeof(s_metrics_topic) - 1U] = '\0';
    s_metrics_topic_length = strlen(s_metrics_topic);
    s_message.topic = s_metrics_topic;
    s_message.topic_length = s_metrics_topic_length;
}

static void tiny_mqtt_publisher_ensure_metrics_topic(void)
{
    if (s_metrics_topic_length != 0U) {
        return;
    }

    const config_manager_mqtt_topics_t *topics = config_manager_get_mqtt_topics();
    if (topics != NULL && topics->metrics[0] != '\0') {
        tiny_mqtt_publisher_set_topic_internal(topics->metrics);
        return;
    }

    tiny_mqtt_publisher_set_topic_internal(NULL);
}

void tiny_mqtt_publisher_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void tiny_mqtt_publisher_reset(void)
{
    tiny_mqtt_publisher_ensure_metrics_topic();
    s_last_publish_ms = 0U;
    memset(s_payload_buffer, 0, sizeof(s_payload_buffer));
    s_message.topic = s_metrics_topic;
    s_message.topic_length = s_metrics_topic_length;
    s_message.payload = s_payload_buffer;
    s_message.payload_length = 0U;
    s_message.qos = s_config.qos;
    s_message.retain = s_config.retain;
}

void tiny_mqtt_publisher_set_metrics_topic(const char *topic)
{
    tiny_mqtt_publisher_set_topic_internal(topic);
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

bool tiny_mqtt_publisher_build_metrics_message(const uart_bms_live_data_t *data,
                                               tiny_mqtt_publisher_message_t *message)
{
    if (data == NULL || message == NULL) {
        return false;
    }

    size_t payload_length = 0U;
    if (!telemetry_json_write_metrics(data, s_payload_buffer, sizeof(s_payload_buffer), &payload_length)) {
        return false;
    }

    tiny_mqtt_publisher_ensure_metrics_topic();

    s_message.payload = s_payload_buffer;
    s_message.payload_length = payload_length;
    s_message.topic = s_metrics_topic;
    s_message.topic_length = s_metrics_topic_length;
    s_message.qos = s_config.qos;
    s_message.retain = s_config.retain;

    if (payload_length < sizeof(s_payload_buffer)) {
        s_payload_buffer[payload_length] = '\0';
    }

    *message = s_message;
    return true;
}

void tiny_mqtt_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;

    if (data == NULL) {
        return;
    }

    uint64_t timestamp_ms = tiny_mqtt_publisher_extract_timestamp(data);
    if (!should_publish(timestamp_ms)) {
        return;
    }

    tiny_mqtt_publisher_event_buffer_t *event_buffer =
        (tiny_mqtt_publisher_event_buffer_t *)malloc(sizeof(tiny_mqtt_publisher_event_buffer_t));
    if (event_buffer == NULL) {
        return;
    }

    size_t payload_length = 0U;
    if (!telemetry_json_write_metrics(
            data, event_buffer->payload, sizeof(event_buffer->payload), &payload_length)) {
        free(event_buffer);
        return;
    }

    if (payload_length < sizeof(event_buffer->payload)) {
        event_buffer->payload[payload_length] = '\0';
    }

    tiny_mqtt_publisher_ensure_metrics_topic();

    event_buffer->message.topic = s_metrics_topic;
    event_buffer->message.topic_length = s_metrics_topic_length;
    event_buffer->message.payload = event_buffer->payload;
    event_buffer->message.payload_length = payload_length;
    event_buffer->message.qos = s_config.qos;
    event_buffer->message.retain = s_config.retain;

    s_last_publish_ms = timestamp_ms;

    if (s_event_publisher == NULL) {
        free(event_buffer);
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_MQTT_METRICS,
        .payload = &event_buffer->message,
        .payload_size = sizeof(event_buffer->message),
        .dispose = tiny_mqtt_publisher_release_event_buffer,
        .dispose_context = event_buffer,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Unable to publish TinyBMS MQTT metrics event");
        tiny_mqtt_publisher_release_event_buffer(event_buffer);
    }
}

void tiny_mqtt_publisher_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing MQTT publisher...");

#if CONFIG_TINYBMS_MQTT_ENABLE
    // Unregister BMS listener if registered
    if (s_listener_registered) {
        uart_bms_unregister_listener(tiny_mqtt_publisher_on_bms_update, NULL);
        s_listener_registered = false;
    }
#endif

    // Reset state
    s_event_publisher = NULL;
    tiny_mqtt_publisher_reset();

    ESP_LOGI(TAG, "MQTT publisher deinitialized");
}

