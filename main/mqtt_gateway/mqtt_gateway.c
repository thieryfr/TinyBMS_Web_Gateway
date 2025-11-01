#include "mqtt_gateway.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_events.h"
#include "can_publisher.h"
#include "config_manager.h"
#include "event_bus.h"
#include "mqtt_client.h"
#include "mqtt_topics.h"

#ifndef CONFIG_TINYBMS_MQTT_ENABLE
#define CONFIG_TINYBMS_MQTT_ENABLE 0
#endif

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#else
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGW(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGE(tag, fmt, ...) (void)(tag), (void)(fmt)
#endif
#endif

static const char *TAG = "mqtt_gateway";

#if CONFIG_TINYBMS_MQTT_ENABLE

typedef struct {
    event_bus_subscription_handle_t subscription;
    TaskHandle_t task;
    mqtt_client_config_t config;
    bool config_valid;
    bool mqtt_started;
    bool wifi_connected;
    char status_topic[96];
    char metrics_topic[96];
    char can_raw_topic[96];
    char can_decoded_topic[96];
    char can_ready_topic[96];
    char config_topic[96];
} mqtt_gateway_ctx_t;

static mqtt_gateway_ctx_t s_gateway = {0};

#ifdef ESP_PLATFORM
static const char *mqtt_gateway_err_to_name(esp_err_t err)
{
    return esp_err_to_name(err);
}
#else
static const char *mqtt_gateway_err_to_name(esp_err_t err)
{
    (void)err;
    return "N/A";
}
#endif

static void mqtt_gateway_format_topics(const char *device_id)
{
    if (device_id == NULL) {
        device_id = "device";
    }

    (void)snprintf(s_gateway.status_topic, sizeof(s_gateway.status_topic), MQTT_TOPIC_FMT_STATUS, device_id);
    (void)snprintf(s_gateway.metrics_topic, sizeof(s_gateway.metrics_topic), MQTT_TOPIC_FMT_METRICS, device_id);
    (void)snprintf(s_gateway.can_raw_topic, sizeof(s_gateway.can_raw_topic), MQTT_TOPIC_FMT_CAN_STREAM, device_id, "raw");
    (void)snprintf(s_gateway.can_decoded_topic, sizeof(s_gateway.can_decoded_topic), MQTT_TOPIC_FMT_CAN_STREAM, device_id, "decoded");
    (void)snprintf(s_gateway.can_ready_topic, sizeof(s_gateway.can_ready_topic), MQTT_TOPIC_FMT_CAN_STREAM, device_id, "ready");
    (void)snprintf(s_gateway.config_topic, sizeof(s_gateway.config_topic), MQTT_TOPIC_FMT_CONFIG, device_id);
}

static size_t mqtt_gateway_string_length(const void *payload, size_t length)
{
    if (payload == NULL) {
        return 0U;
    }

    if (length == 0U) {
        return 0U;
    }

    const uint8_t *bytes = (const uint8_t *)payload;
    while (length > 0U && bytes[length - 1U] == '\0') {
        --length;
    }
    return length;
}

static void mqtt_gateway_publish(const char *topic, const void *payload, size_t length, int qos, bool retain)
{
    if (topic == NULL || payload == NULL || length == 0U) {
        return;
    }

    if (!mqtt_client_publish(topic, payload, length, qos, retain, pdMS_TO_TICKS(200))) {
        ESP_LOGW(TAG, "Failed to publish MQTT payload on '%s'", topic);
    }
}

static void mqtt_gateway_publish_status(const event_bus_event_t *event)
{
    if (event == NULL || event->payload == NULL) {
        return;
    }

    size_t length = mqtt_gateway_string_length(event->payload, event->payload_size);
    if (length == 0U) {
        return;
    }

    bool retain = s_gateway.config.retain_enabled && MQTT_TOPIC_STATUS_RETAIN;
    mqtt_gateway_publish(s_gateway.status_topic,
                         event->payload,
                         length,
                         MQTT_TOPIC_STATUS_QOS,
                         retain);
}

static void mqtt_gateway_publish_config(const event_bus_event_t *event)
{
    if (event == NULL || event->payload == NULL) {
        return;
    }

    size_t length = mqtt_gateway_string_length(event->payload, event->payload_size);
    if (length == 0U) {
        return;
    }

    mqtt_gateway_publish(s_gateway.config_topic,
                         event->payload,
                         length,
                         MQTT_TOPIC_CONFIG_QOS,
                         MQTT_TOPIC_CONFIG_RETAIN);
}

static void mqtt_gateway_publish_can_string(const event_bus_event_t *event, const char *topic)
{
    if (event == NULL || event->payload == NULL || topic == NULL) {
        return;
    }

    size_t length = mqtt_gateway_string_length(event->payload, event->payload_size);
    if (length == 0U) {
        return;
    }

    mqtt_gateway_publish(topic, event->payload, length, MQTT_TOPIC_CAN_QOS, MQTT_TOPIC_CAN_RETAIN);
}

static void mqtt_gateway_publish_can_ready(const can_publisher_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    char buffer[192];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"type\":\"can_ready\",\"id\":\"%08" PRIX32 "\",\"timestamp\":%" PRIu64 ",\"dlc\":%u,\"data\":\"",
                           frame->id,
                           frame->timestamp_ms,
                           (unsigned)frame->dlc);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        ESP_LOGW(TAG, "CAN ready payload truncated for 0x%08" PRIX32, frame->id);
        return;
    }

    size_t offset = (size_t)written;
    for (uint8_t i = 0U; i < frame->dlc && i < sizeof(frame->data); ++i) {
        if (offset + 2U >= sizeof(buffer)) {
            ESP_LOGW(TAG, "CAN ready payload buffer exhausted for 0x%08" PRIX32, frame->id);
            return;
        }

        int hex = snprintf(&buffer[offset], sizeof(buffer) - offset, "%02X", frame->data[i]);
        if (hex < 0 || offset + (size_t)hex >= sizeof(buffer)) {
            ESP_LOGW(TAG, "CAN ready payload formatting failed for 0x%08" PRIX32, frame->id);
            return;
        }
        offset += (size_t)hex;
    }

    if (offset + 3U >= sizeof(buffer)) {
        ESP_LOGW(TAG, "CAN ready payload finalisation failed for 0x%08" PRIX32, frame->id);
        return;
    }

    int tail = snprintf(&buffer[offset], sizeof(buffer) - offset, "\"}");
    if (tail < 0) {
        return;
    }
    offset += (size_t)tail;

    mqtt_gateway_publish(s_gateway.can_ready_topic,
                         buffer,
                         offset,
                         MQTT_TOPIC_CAN_QOS,
                         MQTT_TOPIC_CAN_RETAIN);
}

static void mqtt_gateway_stop_client(void)
{
    if (!s_gateway.mqtt_started) {
        return;
    }

    mqtt_client_stop();
    s_gateway.mqtt_started = false;
    ESP_LOGI(TAG, "MQTT client stopped");
}

static void mqtt_gateway_start_client(void)
{
    if (s_gateway.mqtt_started) {
        return;
    }

    esp_err_t err = mqtt_client_start();
    if (err == ESP_OK) {
        s_gateway.mqtt_started = true;
        ESP_LOGI(TAG, "MQTT client started");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "MQTT client start requested before configuration complete");
    } else {
        ESP_LOGW(TAG, "Failed to start MQTT client: %s", mqtt_gateway_err_to_name(err));
    }
}

static void mqtt_gateway_reload_config(bool restart_client)
{
    const mqtt_client_config_t *cfg = config_manager_get_mqtt_client_config();
    if (cfg == NULL) {
        ESP_LOGW(TAG, "MQTT configuration unavailable");
        return;
    }

    s_gateway.config = *cfg;
    s_gateway.config_valid = true;

    esp_err_t err = mqtt_client_apply_configuration(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply MQTT configuration: %s", mqtt_gateway_err_to_name(err));
        return;
    }

    if (restart_client) {
        if (s_gateway.mqtt_started) {
            mqtt_gateway_stop_client();
        }
        mqtt_gateway_start_client();
    }
}

static void mqtt_gateway_handle_wifi_event(app_event_id_t id)
{
    switch (id) {
        case APP_EVENT_ID_WIFI_STA_GOT_IP:
            s_gateway.wifi_connected = true;
            mqtt_gateway_start_client();
            break;
        case APP_EVENT_ID_WIFI_STA_DISCONNECTED:
        case APP_EVENT_ID_WIFI_STA_LOST_IP:
            s_gateway.wifi_connected = false;
            mqtt_gateway_stop_client();
            break;
        default:
            break;
    }
}

static void mqtt_gateway_handle_event(const event_bus_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->id) {
        case APP_EVENT_ID_TELEMETRY_SAMPLE:
            mqtt_gateway_publish_status(event);
            break;
        case APP_EVENT_ID_CONFIG_UPDATED:
            mqtt_gateway_publish_config(event);
            mqtt_gateway_reload_config(true);
            break;
        case APP_EVENT_ID_CAN_FRAME_RAW:
            mqtt_gateway_publish_can_string(event, s_gateway.can_raw_topic);
            break;
        case APP_EVENT_ID_CAN_FRAME_DECODED:
            mqtt_gateway_publish_can_string(event, s_gateway.can_decoded_topic);
            break;
        case APP_EVENT_ID_CAN_FRAME_READY:
            if (event->payload != NULL && event->payload_size == sizeof(can_publisher_frame_t)) {
                const can_publisher_frame_t *frame = (const can_publisher_frame_t *)event->payload;
                mqtt_gateway_publish_can_ready(frame);
            }
            break;
        case APP_EVENT_ID_WIFI_STA_GOT_IP:
        case APP_EVENT_ID_WIFI_STA_DISCONNECTED:
        case APP_EVENT_ID_WIFI_STA_LOST_IP:
            mqtt_gateway_handle_wifi_event((app_event_id_t)event->id);
            break;
        default:
            break;
    }
}

static void mqtt_gateway_event_task(void *context)
{
    (void)context;

    if (s_gateway.subscription == NULL) {
        vTaskDelete(NULL);
        return;
    }

    event_bus_event_t event = {0};
    while (event_bus_receive(s_gateway.subscription, &event, portMAX_DELAY)) {
        mqtt_gateway_handle_event(&event);
    }

    vTaskDelete(NULL);
}

void mqtt_gateway_init(void)
{
    mqtt_gateway_format_topics(APP_DEVICE_NAME);
    mqtt_gateway_reload_config(false);

    s_gateway.subscription = event_bus_subscribe(16, NULL, NULL);
    if (s_gateway.subscription == NULL) {
        ESP_LOGW(TAG, "Unable to subscribe to event bus; MQTT gateway disabled");
        return;
    }

    if (xTaskCreate(mqtt_gateway_event_task, "mqtt_evt", 4096, NULL, 5, &s_gateway.task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT gateway task");
    }

    mqtt_gateway_start_client();
}

#else

void mqtt_gateway_init(void)
{
    ESP_LOGI(TAG, "MQTT gateway support disabled in configuration");
}

#endif

