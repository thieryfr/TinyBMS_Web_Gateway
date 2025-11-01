#include "mqtt_client.h"
#include "mqtt_topics.h"

#include <string.h>

#include "freertos/semphr.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include_next "mqtt_client.h"
#else
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGW(tag, fmt, ...) (void)(tag), (void)(fmt)
#define ESP_LOGE(tag, fmt, ...) (void)(tag), (void)(fmt)
#endif
#endif

typedef struct {
    esp_mqtt_client_handle_t client;
    SemaphoreHandle_t lock;
    event_bus_publish_fn_t event_publisher;
    mqtt_client_event_listener_t listener;
    bool initialised;
    bool started;
} mqtt_client_ctx_t;

static mqtt_client_ctx_t s_ctx = {
    .client = NULL,
    .lock = NULL,
    .event_publisher = NULL,
    .listener = {0},
    .initialised = false,
    .started = false,
};

static const char *TAG = "mqtt_client";

static bool mqtt_client_lock(TickType_t timeout)
{
    if (s_ctx.lock == NULL) {
        return false;
    }
    return xSemaphoreTake(s_ctx.lock, timeout) == pdTRUE;
}

static void mqtt_client_unlock(void)
{
    if (s_ctx.lock != NULL) {
        (void)xSemaphoreGive(s_ctx.lock);
    }
}

void mqtt_client_set_event_publisher(event_bus_publish_fn_t publisher)
{
    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
        if (s_ctx.lock == NULL) {
            return;
        }
    }

    if (!mqtt_client_lock(portMAX_DELAY)) {
        return;
    }

    s_ctx.event_publisher = publisher;

    mqtt_client_unlock();
}

esp_err_t mqtt_client_init(const mqtt_client_event_listener_t *listener)
{
    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
        if (s_ctx.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!mqtt_client_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_INVALID_STATE;
    }

    if (listener != NULL) {
        s_ctx.listener = *listener;
    } else {
        memset(&s_ctx.listener, 0, sizeof(s_ctx.listener));
    }

    s_ctx.initialised = true;

    mqtt_client_unlock();

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "MQTT client initialised (handle pending configuration)");
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

esp_err_t mqtt_client_start(void)
{
    if (!s_ctx.initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mqtt_client_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.started) {
        mqtt_client_unlock();
        return ESP_OK;
    }

#ifdef ESP_PLATFORM
    if (s_ctx.client != NULL) {
        esp_err_t err = esp_mqtt_client_start(s_ctx.client);
        if (err != ESP_OK) {
            mqtt_client_unlock();
            return err;
        }
    } else {
        ESP_LOGW(TAG, "MQTT client handle not configured, start deferred");
    }
#endif

    s_ctx.started = true;

    mqtt_client_unlock();

    return ESP_OK;
}

void mqtt_client_stop(void)
{
    if (!s_ctx.initialised) {
        return;
    }

    if (!mqtt_client_lock(pdMS_TO_TICKS(100))) {
        return;
    }

    if (!s_ctx.started) {
        mqtt_client_unlock();
        return;
    }

#ifdef ESP_PLATFORM
    if (s_ctx.client != NULL) {
        esp_err_t err = esp_mqtt_client_stop(s_ctx.client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop MQTT client: %d", (int)err);
        }
    }
#endif

    s_ctx.started = false;

    mqtt_client_unlock();
}

bool mqtt_client_publish(const char *topic,
                         const void *payload,
                         size_t payload_length,
                         int qos,
                         bool retain,
                         TickType_t timeout)
{
    if (topic == NULL || payload == NULL) {
        return false;
    }

    if (!s_ctx.initialised) {
        return false;
    }

    if (!mqtt_client_lock(timeout)) {
        return false;
    }

    bool result = false;

    if (!s_ctx.started || s_ctx.client == NULL) {
        goto exit;
    }

#ifdef ESP_PLATFORM
    int msg_id = esp_mqtt_client_publish(s_ctx.client, topic, payload, (int)payload_length, qos, retain);
    result = (msg_id >= 0);
    if (!result) {
        ESP_LOGW(TAG, "Failed to publish MQTT message on topic '%s'", topic);
    }
#else
    (void)qos;
    (void)retain;
    (void)payload_length;
    result = false;
#endif

exit:
    mqtt_client_unlock();
    return result;
}
