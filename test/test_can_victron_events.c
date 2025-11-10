#include "unity.h"

#include "app_events.h"
#include "can_victron.h"
#include "event_bus.h"

#include <string.h>

typedef struct {
    event_bus_event_id_t id;
    char payload[256];
} captured_event_t;

static captured_event_t s_events[4];
static size_t s_event_count = 0;

static bool capture_event(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;
    if (event == NULL || s_event_count >= (sizeof(s_events) / sizeof(s_events[0]))) {
        return false;
    }

    captured_event_t *slot = &s_events[s_event_count];
    slot->id = event->id;

    size_t length = 0;
    if (event->payload != NULL && event->payload_size > 0) {
        length = strnlen((const char *)event->payload, event->payload_size);
        if (length >= sizeof(slot->payload)) {
            length = sizeof(slot->payload) - 1U;
        }
        memcpy(slot->payload, event->payload, length);
    }
    slot->payload[length] = '\0';

    s_event_count++;
    return true;
}

TEST_CASE("can_victron_publish_frame exposes timestamp fields", "[can][victron]")
{
    memset(s_events, 0, sizeof(s_events));
    s_event_count = 0;

    can_victron_set_event_publisher(capture_event);

    const uint8_t data[3] = {0x11, 0x22, 0x33};
    TEST_ASSERT_EQUAL(ESP_OK, can_victron_publish_frame(0x18FF50E5, data, sizeof(data), "unit test frame"));

    TEST_ASSERT_TRUE(s_event_count >= 2U);

    const char *raw_payload = NULL;
    const char *decoded_payload = NULL;

    for (size_t i = 0; i < s_event_count; ++i) {
        if (s_events[i].id == APP_EVENT_ID_CAN_FRAME_RAW) {
            raw_payload = s_events[i].payload;
        } else if (s_events[i].id == APP_EVENT_ID_CAN_FRAME_DECODED) {
            decoded_payload = s_events[i].payload;
        }
    }

    TEST_ASSERT_NOT_NULL(raw_payload);
    TEST_ASSERT_NOT_NULL(decoded_payload);

    TEST_ASSERT_NOT_EQUAL(0, strstr(raw_payload, "\"timestamp_ms\":"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(raw_payload, "\"timestamp\":"));

    TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"timestamp_ms\":"));
    TEST_ASSERT_NOT_EQUAL(0, strstr(decoded_payload, "\"timestamp\":"));

    can_victron_set_event_publisher(NULL);
}
