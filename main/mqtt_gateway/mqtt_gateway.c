#include "mqtt_gateway.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_events.h"
#include "alert_manager.h"
#include "can_publisher.h"
#include "config_manager.h"
#include "event_bus.h"
#include "mqtt_client.h"
#include "mqtt_topics.h"
#include "tiny_mqtt_publisher.h"

#ifndef CONFIG_TINYBMS_MQTT_ENABLE
#define CONFIG_TINYBMS_MQTT_ENABLE 0
#endif

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
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
    SemaphoreHandle_t lock;
    mqtt_client_config_t config;
    bool config_valid;
    bool mqtt_started;
    bool wifi_connected;
    bool connected;
    uint32_t reconnect_count;
    uint32_t disconnect_count;
    uint32_t error_count;
    mqtt_client_event_id_t last_event;
    int64_t last_event_timestamp_us;
    char last_error[96];
    char status_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char metrics_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_raw_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_decoded_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char can_ready_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char config_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char alerts_topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
} mqtt_gateway_ctx_t;

static mqtt_gateway_ctx_t s_gateway = {0};

static bool mqtt_gateway_lock_ctx(TickType_t timeout)
{
    if (s_gateway.lock == NULL) {
        return false;
    }
    return xSemaphoreTake(s_gateway.lock, timeout) == pdTRUE;
}

static void mqtt_gateway_unlock_ctx(void)
{
    if (s_gateway.lock != NULL) {
        (void)xSemaphoreGive(s_gateway.lock);
    }
}

static void mqtt_gateway_set_topic(char *dest, size_t dest_size, const char *value, const char *fallback)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    const char *source = (value != NULL && value[0] != '\0') ? value : fallback;
    if (source == NULL) {
        dest[0] = '\0';
        return;
    }

    size_t len = strlen(source);
    if (len >= dest_size) {
        len = dest_size - 1U;
    }
    memcpy(dest, source, len);
    dest[len] = '\0';
}

static void mqtt_gateway_load_topics(void);
static void mqtt_gateway_record_event(mqtt_client_event_id_t id, const char *error);
static void mqtt_gateway_on_mqtt_event(const mqtt_client_event_t *event, void *context);

static const mqtt_client_event_listener_t s_mqtt_listener = {
    .callback = mqtt_gateway_on_mqtt_event,
    .context = NULL,
};

const mqtt_client_event_listener_t *mqtt_gateway_get_event_listener(void)
{
    return &s_mqtt_listener;
}

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
    (void)snprintf(s_gateway.alerts_topic, sizeof(s_gateway.alerts_topic), "%s/alerts", device_id);
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

    char topic[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    bool retain_flag = false;

    // Augmenter timeout à 100ms et retourner erreur si échec (pas d'accès sans lock)
    if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(100))) {
        strncpy(topic, s_gateway.status_topic, sizeof(topic));
        topic[sizeof(topic) - 1U] = '\0';
        retain_flag = s_gateway.config.retain_enabled;
        mqtt_gateway_unlock_ctx();
    } else {
        ESP_LOGW(TAG, "Failed to acquire gateway lock, aborting publish");
        return;  // Retourner erreur au lieu d'accéder sans lock
    }

    bool retain = retain_flag && MQTT_TOPIC_STATUS_RETAIN;
    mqtt_gateway_publish(topic,
                         event->payload,
                         length,
                         MQTT_TOPIC_STATUS_QOS,
                         retain);
}

static void mqtt_gateway_publish_metrics_message(const tiny_mqtt_publisher_message_t *message)
{
    if (message == NULL || message->payload == NULL || message->payload_length == 0U) {
        return;
    }

    int qos = message->qos;
    if (qos < 0) {
        qos = 0;
    } else if (qos > 2) {
        qos = 2;
    }

    mqtt_gateway_publish(s_gateway.metrics_topic,
                         message->payload,
                         message->payload_length,
                         qos,
                         message->retain);
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

static void mqtt_gateway_publish_alert(const event_bus_event_t *event)
{
    if (event == NULL || event->payload == NULL) {
        return;
    }

    // Parse alert entry from JSON payload
    const char *json_payload = (const char *)event->payload;
    size_t length = mqtt_gateway_string_length(event->payload, event->payload_size);

    if (length == 0U || length > 512U) {
        return;
    }

    // Forward the JSON payload directly to MQTT
    // The payload is already in JSON format from alert_manager
    mqtt_gateway_publish(s_gateway.alerts_topic,
                         json_payload,
                         length,
                         1,  // QoS 1 for alerts (at least once delivery)
                         false);  // No retain for alerts
}

static void mqtt_gateway_load_topics(void)
{
    const config_manager_mqtt_topics_t *topics = config_manager_get_mqtt_topics();

    char fallback_status[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_metrics[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_config[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_can_raw[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_can_decoded[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_can_ready[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];
    char fallback_alerts[CONFIG_MANAGER_MQTT_TOPIC_MAX_LENGTH];

    (void)snprintf(fallback_status, sizeof(fallback_status), MQTT_TOPIC_FMT_STATUS, APP_DEVICE_NAME);
    (void)snprintf(fallback_metrics, sizeof(fallback_metrics), MQTT_TOPIC_FMT_METRICS, APP_DEVICE_NAME);
    (void)snprintf(fallback_config, sizeof(fallback_config), MQTT_TOPIC_FMT_CONFIG, APP_DEVICE_NAME);
    (void)snprintf(fallback_can_raw, sizeof(fallback_can_raw), MQTT_TOPIC_FMT_CAN_STREAM, APP_DEVICE_NAME, "raw");
    (void)snprintf(fallback_can_decoded, sizeof(fallback_can_decoded), MQTT_TOPIC_FMT_CAN_STREAM, APP_DEVICE_NAME, "decoded");
    (void)snprintf(fallback_can_ready, sizeof(fallback_can_ready), MQTT_TOPIC_FMT_CAN_STREAM, APP_DEVICE_NAME, "ready");
    (void)snprintf(fallback_alerts, sizeof(fallback_alerts), "%s/alerts", APP_DEVICE_NAME);

    if (!mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        return;
    }

    mqtt_gateway_set_topic(s_gateway.status_topic,
                           sizeof(s_gateway.status_topic),
                           topics != NULL ? topics->status : NULL,
                           fallback_status);
    mqtt_gateway_set_topic(s_gateway.metrics_topic,
                           sizeof(s_gateway.metrics_topic),
                           topics != NULL ? topics->metrics : NULL,
                           fallback_metrics);
    mqtt_gateway_set_topic(s_gateway.config_topic,
                           sizeof(s_gateway.config_topic),
                           topics != NULL ? topics->config : NULL,
                           fallback_config);
    mqtt_gateway_set_topic(s_gateway.can_raw_topic,
                           sizeof(s_gateway.can_raw_topic),
                           topics != NULL ? topics->can_raw : NULL,
                           fallback_can_raw);
    mqtt_gateway_set_topic(s_gateway.can_decoded_topic,
                           sizeof(s_gateway.can_decoded_topic),
                           topics != NULL ? topics->can_decoded : NULL,
                           fallback_can_decoded);
    mqtt_gateway_set_topic(s_gateway.can_ready_topic,
                           sizeof(s_gateway.can_ready_topic),
                           topics != NULL ? topics->can_ready : NULL,
                           fallback_can_ready);
    mqtt_gateway_set_topic(s_gateway.alerts_topic,
                           sizeof(s_gateway.alerts_topic),
                           NULL,
                           fallback_alerts);

    mqtt_gateway_unlock_ctx();
}

static void mqtt_gateway_record_event(mqtt_client_event_id_t id, const char *error)
{
    if (!mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        return;
    }

    s_gateway.last_event = id;
#if defined(ESP_PLATFORM)
    s_gateway.last_event_timestamp_us = esp_timer_get_time();
#else
    s_gateway.last_event_timestamp_us = 0;
#endif

    switch (id) {
        case MQTT_CLIENT_EVENT_CONNECTED:
            s_gateway.connected = true;
            s_gateway.reconnect_count += 1U;
            if (error == NULL) {
                s_gateway.last_error[0] = '\0';
            }
            break;
        case MQTT_CLIENT_EVENT_DISCONNECTED:
            s_gateway.connected = false;
            s_gateway.disconnect_count += 1U;
            break;
        case MQTT_CLIENT_EVENT_ERROR:
            s_gateway.error_count += 1U;
            break;
        default:
            break;
    }

    if (error != NULL) {
        size_t len = strlen(error);
        if (len >= sizeof(s_gateway.last_error)) {
            len = sizeof(s_gateway.last_error) - 1U;
        }
        memcpy(s_gateway.last_error, error, len);
        s_gateway.last_error[len] = '\0';
    }

    mqtt_gateway_unlock_ctx();
}

static void mqtt_gateway_on_mqtt_event(const mqtt_client_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }

    const char *message = NULL;
    switch (event->id) {
        case MQTT_CLIENT_EVENT_CONNECTED:
            message = NULL;
            break;
        case MQTT_CLIENT_EVENT_DISCONNECTED:
            message = "MQTT client disconnected";
            break;
        case MQTT_CLIENT_EVENT_ERROR:
            message = "MQTT client error";
            break;
        default:
            message = NULL;
            break;
    }

    mqtt_gateway_record_event(event->id, message);
}

static void mqtt_gateway_stop_client(void)
{
    if (!mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        return;
    }

    bool was_started = s_gateway.mqtt_started;
    if (was_started) {
        s_gateway.mqtt_started = false;
    }
    mqtt_gateway_unlock_ctx();

    if (!was_started) {
        return;
    }

    mqtt_client_stop();
    ESP_LOGI(TAG, "MQTT client stopped");
}

static void mqtt_gateway_start_client(void)
{
    if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        if (s_gateway.mqtt_started) {
            mqtt_gateway_unlock_ctx();
            return;
        }
        mqtt_gateway_unlock_ctx();
    } else {
        return;
    }

    esp_err_t err = mqtt_client_start();
    if (err == ESP_OK) {
        if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
            s_gateway.mqtt_started = true;
            mqtt_gateway_unlock_ctx();
        }
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

    mqtt_client_config_t snapshot = *cfg;

    esp_err_t err = mqtt_client_apply_configuration(&snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply MQTT configuration: %s", mqtt_gateway_err_to_name(err));
        return;
    }

    if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        s_gateway.config = snapshot;
        s_gateway.config_valid = true;
        mqtt_gateway_unlock_ctx();
    }

    tiny_mqtt_publisher_config_t metrics_cfg = {
        .publish_interval_ms = TINY_MQTT_PUBLISH_INTERVAL_KEEP,
        .qos = (int)snapshot.default_qos,
        .retain = MQTT_TOPIC_METRICS_RETAIN,
    };
    tiny_mqtt_publisher_apply_config(&metrics_cfg);

    mqtt_gateway_load_topics();

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
            if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
                s_gateway.wifi_connected = true;
                mqtt_gateway_unlock_ctx();
            }
            mqtt_gateway_start_client();
            break;
        case APP_EVENT_ID_WIFI_STA_DISCONNECTED:
        case APP_EVENT_ID_WIFI_STA_LOST_IP:
            if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
                s_gateway.wifi_connected = false;
                mqtt_gateway_unlock_ctx();
            }
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
        case APP_EVENT_ID_MQTT_METRICS:
            if (event->payload != NULL && event->payload_size == sizeof(tiny_mqtt_publisher_message_t)) {
                const tiny_mqtt_publisher_message_t *message =
                    (const tiny_mqtt_publisher_message_t *)event->payload;
                mqtt_gateway_publish_metrics_message(message);
            }
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
        case APP_EVENT_ID_ALERT_TRIGGERED:
            mqtt_gateway_publish_alert(event);
            break;
        default:
            break;
    }
}

void mqtt_gateway_get_status(mqtt_gateway_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));

    if (!mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        return;
    }

    status->client_started = s_gateway.mqtt_started;
    status->connected = s_gateway.connected;
    status->wifi_connected = s_gateway.wifi_connected;
    status->reconnect_count = s_gateway.reconnect_count;
    status->disconnect_count = s_gateway.disconnect_count;
    status->error_count = s_gateway.error_count;
    status->last_event = s_gateway.last_event;
    status->last_event_timestamp_ms = (s_gateway.last_event_timestamp_us >= 0)
                                         ? (uint64_t)(s_gateway.last_event_timestamp_us / 1000)
                                         : 0;

    strncpy(status->broker_uri, s_gateway.config.broker_uri, sizeof(status->broker_uri) - 1U);
    strncpy(status->status_topic, s_gateway.status_topic, sizeof(status->status_topic) - 1U);
    strncpy(status->metrics_topic, s_gateway.metrics_topic, sizeof(status->metrics_topic) - 1U);
    strncpy(status->config_topic, s_gateway.config_topic, sizeof(status->config_topic) - 1U);
    strncpy(status->can_raw_topic, s_gateway.can_raw_topic, sizeof(status->can_raw_topic) - 1U);
    strncpy(status->can_decoded_topic, s_gateway.can_decoded_topic, sizeof(status->can_decoded_topic) - 1U);
    strncpy(status->can_ready_topic, s_gateway.can_ready_topic, sizeof(status->can_ready_topic) - 1U);
    strncpy(status->last_error, s_gateway.last_error, sizeof(status->last_error) - 1U);

    mqtt_gateway_unlock_ctx();
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
    s_gateway.lock = xSemaphoreCreateMutex();
    if (s_gateway.lock == NULL) {
        ESP_LOGW(TAG, "Unable to create MQTT gateway mutex");
        return;
    }

    if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(50))) {
        s_gateway.last_event = MQTT_CLIENT_EVENT_DISCONNECTED;
        s_gateway.last_event_timestamp_us = 0;
        s_gateway.last_error[0] = '\0';
        mqtt_gateway_unlock_ctx();
    }

    mqtt_gateway_load_topics();
    mqtt_gateway_reload_config(false);

    s_gateway.subscription = event_bus_subscribe(32, NULL, NULL);
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

const mqtt_client_event_listener_t *mqtt_gateway_get_event_listener(void)
{
    return NULL;
}

void mqtt_gateway_get_status(mqtt_gateway_status_t *status)
{
    if (status != NULL) {
        memset(status, 0, sizeof(*status));
    }
}

void mqtt_gateway_init(void)
{
    ESP_LOGI(TAG, "MQTT gateway support disabled in configuration");
}

#endif

