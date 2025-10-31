#include "can_victron.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
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

#define CAN_VICTRON_EVENT_BUFFERS 4
#define CAN_VICTRON_JSON_SIZE     256

static const char *TAG = "can_victron";

static event_bus_publish_fn_t s_event_publisher = NULL;
static char s_can_raw_events[CAN_VICTRON_EVENT_BUFFERS][CAN_VICTRON_JSON_SIZE];
static char s_can_decoded_events[CAN_VICTRON_EVENT_BUFFERS][CAN_VICTRON_JSON_SIZE];
static size_t s_next_event_slot = 0;

static uint64_t can_victron_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

static bool can_victron_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
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

static void can_victron_publish_event(event_bus_event_id_t id, char *payload, size_t length)
{
    if (s_event_publisher == NULL || payload == NULL || length == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = id,
        .payload = payload,
        .payload_size = length + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish CAN event %u", (unsigned)id);
    }
}

static void can_victron_publish_demo_frames(void)
{
    static const uint8_t k_demo_status[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    can_victron_publish_frame(0x18FF50E5, k_demo_status, sizeof(k_demo_status), "Battery status frame");

    static const uint8_t k_demo_alarm[] = {0x01, 0x02, 0x00, 0x00};
    can_victron_publish_frame(0x18FF01E5, k_demo_alarm, sizeof(k_demo_alarm), "Alarm flags");
}

void can_victron_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

esp_err_t can_victron_publish_frame(uint32_t can_id,
                                    const uint8_t *data,
                                    size_t length,
                                    const char *description)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length > 8) {
        length = 8;
    }

    if (s_event_publisher == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t timestamp = can_victron_timestamp_ms();

    size_t raw_index = s_next_event_slot;
    s_next_event_slot = (s_next_event_slot + 1) % CAN_VICTRON_EVENT_BUFFERS;
    char *raw_payload = s_can_raw_events[raw_index];
    size_t raw_offset = 0;

    if (!can_victron_json_append(raw_payload,
                                 CAN_VICTRON_JSON_SIZE,
                                 &raw_offset,
                                 "{\"type\":\"can_raw\",\"timestamp\":%" PRIu64 ",\"id\":\"%08" PRIX32 "\","
                                 "\"dlc\":%zu,\"data\":\"",
                                 timestamp,
                                 can_id,
                                 length)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < length; ++i) {
        if (!can_victron_json_append(raw_payload,
                                     CAN_VICTRON_JSON_SIZE,
                                     &raw_offset,
                                     "%02X",
                                     (unsigned)data[i])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!can_victron_json_append(raw_payload, CAN_VICTRON_JSON_SIZE, &raw_offset, "\"}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    can_victron_publish_event(APP_EVENT_ID_CAN_FRAME_RAW, raw_payload, raw_offset);

    size_t decoded_index = s_next_event_slot;
    s_next_event_slot = (s_next_event_slot + 1) % CAN_VICTRON_EVENT_BUFFERS;
    char *decoded_payload = s_can_decoded_events[decoded_index];
    size_t decoded_offset = 0;

    const char *label = (description != NULL) ? description : "";
    if (!can_victron_json_append(decoded_payload,
                                 CAN_VICTRON_JSON_SIZE,
                                 &decoded_offset,
                                 "{\"type\":\"can_decoded\",\"timestamp\":%" PRIu64 ",\"id\":\"%08" PRIX32 "\","
                                 "\"description\":\"%s\",\"bytes\":[",
                                 timestamp,
                                 can_id,
                                 label)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < length; ++i) {
        if (!can_victron_json_append(decoded_payload,
                                     CAN_VICTRON_JSON_SIZE,
                                     &decoded_offset,
                                     "%s%u",
                                     (i == 0) ? "" : ",",
                                     (unsigned)data[i])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!can_victron_json_append(decoded_payload, CAN_VICTRON_JSON_SIZE, &decoded_offset, "]}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    can_victron_publish_event(APP_EVENT_ID_CAN_FRAME_DECODED, decoded_payload, decoded_offset);
    return ESP_OK;
}

void can_victron_init(void)
{
    ESP_LOGI(TAG, "Victron CAN monitor initialised");
    can_victron_publish_demo_frames();
}
