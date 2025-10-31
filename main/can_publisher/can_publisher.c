#include "can_publisher.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <sys/time.h>
#endif

#include "freertos/FreeRTOS.h"

#include "app_events.h"
#include "conversion_table.h"

#define CAN_PUBLISHER_EVENT_TIMEOUT_MS 50U

static const char *TAG = "can_pub";

static event_bus_publish_fn_t s_event_publisher = NULL;
static can_publisher_frame_publish_fn_t s_frame_publisher = NULL;
static can_publisher_buffer_t s_frame_buffer = {
    .slots = {0},
    .capacity = 0,
    .next_index = 0,
};
static can_publisher_registry_t s_registry = {
    .channels = NULL,
    .channel_count = 0,
    .buffer = &s_frame_buffer,
};

static uint64_t can_publisher_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

void can_publisher_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

static void can_publisher_publish_event(const can_publisher_frame_t *frame)
{
    if (s_event_publisher == NULL || frame == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CAN_FRAME_READY,
        .payload = frame,
        .payload_size = sizeof(*frame),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(CAN_PUBLISHER_EVENT_TIMEOUT_MS))) {
        ESP_LOGW(TAG, "Failed to publish CAN frame event for ID 0x%08" PRIX32, frame->id);
    }
}

static void can_publisher_dispatch_frame(const can_publisher_channel_t *channel,
                                         can_publisher_frame_t *frame)
{
    if (channel == NULL || frame == NULL) {
        return;
    }

    if (s_frame_publisher != NULL) {
        esp_err_t err = s_frame_publisher(channel->can_id, frame->data, frame->dlc, channel->description);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to publish CAN frame 0x%08" PRIX32 ": %s",
                     channel->can_id,
                     esp_err_to_name(err));
        }
    }

    can_publisher_publish_event(frame);
}

void can_publisher_init(event_bus_publish_fn_t publisher,
                        can_publisher_frame_publish_fn_t frame_publisher)
{
    can_publisher_set_event_publisher(publisher);
    s_frame_publisher = frame_publisher;

    s_registry.channels = g_can_publisher_channels;
    s_registry.channel_count = g_can_publisher_channel_count;

    if (s_registry.channel_count > CAN_PUBLISHER_MAX_BUFFER_SLOTS) {
        ESP_LOGW(TAG,
                 "Configured %zu CAN channels exceeds buffer capacity (%u), truncating",
                 s_registry.channel_count,
                 (unsigned)CAN_PUBLISHER_MAX_BUFFER_SLOTS);
        s_registry.channel_count = CAN_PUBLISHER_MAX_BUFFER_SLOTS;
    }

    s_frame_buffer.capacity = s_registry.channel_count;
    s_frame_buffer.next_index = 0;

    esp_err_t err = uart_bms_register_listener(can_publisher_on_bms_update, &s_registry);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "CAN publisher initialised with %zu channels", s_registry.channel_count);
    }
}

void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    can_publisher_registry_t *registry = (can_publisher_registry_t *)context;

    if (data == NULL || registry == NULL || registry->channels == NULL || registry->buffer == NULL) {
        return;
    }

    if (registry->channel_count == 0 || registry->buffer->capacity == 0) {
        return;
    }

    uint64_t timestamp_ms = (data->timestamp_ms > 0U) ? data->timestamp_ms : can_publisher_timestamp_ms();

    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];

        if (channel->fill_fn == NULL) {
            continue;
        }

        can_publisher_buffer_t *buffer = registry->buffer;
        can_publisher_frame_t *frame = &buffer->slots[buffer->next_index];

        frame->id = channel->can_id;
        frame->dlc = (channel->dlc > 8U) ? 8U : channel->dlc;
        memset(frame->data, 0, sizeof(frame->data));
        frame->timestamp_ms = timestamp_ms;

        bool encoded = channel->fill_fn(data, frame);
        if (!encoded) {
            ESP_LOGW(TAG, "Encoder rejected TinyBMS sample for CAN ID 0x%08" PRIX32, channel->can_id);
            continue;
        }

        buffer->next_index = (buffer->next_index + 1U) % buffer->capacity;

        can_publisher_dispatch_frame(channel, frame);
    }
}

