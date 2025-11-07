#include "mqtt_client.h"
#include "mqtt_topics.h"

#include <inttypes.h>
#include <string.h>

#include "freertos/semphr.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_event.h"
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

// Spinlock pour protéger la création du mutex (évite race condition)
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;

#ifdef ESP_PLATFORM
static void mqtt_client_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
#endif

static void mqtt_client_publish_simple_event(mqtt_client_event_id_t id)
{
    if (s_ctx.event_publisher == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = (event_bus_event_id_t)id,
        .payload = NULL,
        .payload_size = 0,
    };

    if (!s_ctx.event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish MQTT client event 0x%08" PRIx32, (uint32_t)id);
    }
}

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
    // Protéger la création du mutex avec spinlock (évite race condition)
    portENTER_CRITICAL(&s_init_lock);
    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_init_lock);

    if (s_ctx.lock == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT mutex");
        return;
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

esp_err_t mqtt_client_apply_configuration(const mqtt_client_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
        if (s_ctx.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!mqtt_client_lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_INVALID_STATE;
    }

#ifdef ESP_PLATFORM
    if (s_ctx.started && s_ctx.client != NULL) {
        esp_err_t stop_err = esp_mqtt_client_stop(s_ctx.client);
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to stop MQTT client before reconfiguration: %s",
                     esp_err_to_name(stop_err));
        }
        s_ctx.started = false;
    }

    if (s_ctx.client != NULL) {
        esp_mqtt_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
    }

    esp_mqtt_client_config_t esp_config = {
        .broker.address.uri = config->broker_uri,
        .session.keepalive = config->keepalive_seconds,
    };

    if (config->username[0] != '\0') {
        esp_config.credentials.username = config->username;
    }

    if (config->password[0] != '\0') {
        esp_config.credentials.authentication.password = config->password;
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&esp_config);
    if (client == NULL) {
        mqtt_client_unlock();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t handler_err = esp_mqtt_client_register_event(client,
                                                            ESP_EVENT_ANY_ID,
                                                            mqtt_client_event_handler,
                                                            NULL);
    if (handler_err != ESP_OK) {
        esp_mqtt_client_destroy(client);
        mqtt_client_unlock();
        return handler_err;
    }

    s_ctx.client = client;
#else
    (void)config;
#endif

    mqtt_client_unlock();

    ESP_LOGI(TAG, "MQTT client configured for broker '%s'", config->broker_uri);
    return ESP_OK;
}

void mqtt_client_get_state(mqtt_client_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));

    state->lock_created = (s_ctx.lock != NULL);

    bool locked = mqtt_client_lock(pdMS_TO_TICKS(10));

    state->initialised = s_ctx.initialised;
    state->started = s_ctx.started;
    state->client_handle_created = (s_ctx.client != NULL);
    state->listener_registered = (s_ctx.listener.callback != NULL);
    state->event_publisher_registered = (s_ctx.event_publisher != NULL);

    if (locked) {
        mqtt_client_unlock();
    }
}

#ifdef ESP_PLATFORM
static void mqtt_client_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;

    if (base != MQTT_EVENTS) {
        return;
    }

    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL) {
        return;
    }

    mqtt_client_event_t client_event = {
        .id = MQTT_CLIENT_EVENT_ERROR,
        .payload = event->data,
        .payload_size = (size_t)event->data_len,
    };

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            client_event.id = MQTT_CLIENT_EVENT_CONNECTED;
            client_event.payload = NULL;
            client_event.payload_size = 0;
            ESP_LOGI(TAG, "Connected to MQTT broker");
            mqtt_client_publish_simple_event(client_event.id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            client_event.id = MQTT_CLIENT_EVENT_DISCONNECTED;
            client_event.payload = NULL;
            client_event.payload_size = 0;
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            mqtt_client_publish_simple_event(client_event.id);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            client_event.id = MQTT_CLIENT_EVENT_SUBSCRIBED;
            ESP_LOGI(TAG, "Subscription acknowledged, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            client_event.id = MQTT_CLIENT_EVENT_PUBLISHED;
            ESP_LOGI(TAG, "Message published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            client_event.id = MQTT_CLIENT_EVENT_DATA;
            ESP_LOGI(TAG, "Received MQTT data on topic %.*s", event->topic_len, event->topic);
            break;
        case MQTT_EVENT_ERROR:
            client_event.id = MQTT_CLIENT_EVENT_ERROR;
            if (event->error_handle != NULL) {
                ESP_LOGE(TAG,
                         "MQTT error type 0x%x, rc=%d",
                         event->error_handle->error_type,
                         event->error_handle->connect_return_code);
            } else {
                ESP_LOGE(TAG, "MQTT client reported an unspecified error");
            }
            break;
        default:
            return;
    }

    if (s_ctx.listener.callback != NULL) {
        s_ctx.listener.callback(&client_event, s_ctx.listener.context);
    }
}
#endif
