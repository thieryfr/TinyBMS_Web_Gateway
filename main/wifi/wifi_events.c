#include "wifi_events.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#define WIFI_EVENT_METADATA_SLOTS 16U

typedef struct {
    app_event_id_t id;
    const char *key;
    const char *label;
} wifi_event_descriptor_t;

static const wifi_event_descriptor_t s_wifi_event_descriptors[] = {
    {APP_EVENT_ID_WIFI_STA_START, "wifi_sta_start", "Station interface starting"},
    {APP_EVENT_ID_WIFI_STA_CONNECTED, "wifi_sta_connected", "Station connected"},
    {APP_EVENT_ID_WIFI_STA_DISCONNECTED, "wifi_sta_disconnected", "Station disconnected"},
    {APP_EVENT_ID_WIFI_STA_GOT_IP, "wifi_sta_got_ip", "Station obtained IPv4"},
    {APP_EVENT_ID_WIFI_STA_LOST_IP, "wifi_sta_lost_ip", "Station lost IPv4"},
    {APP_EVENT_ID_WIFI_AP_STARTED, "wifi_ap_started", "Fallback AP started"},
    {APP_EVENT_ID_WIFI_AP_STOPPED, "wifi_ap_stopped", "Fallback AP stopped"},
    {APP_EVENT_ID_WIFI_AP_FAILED, "wifi_ap_failed", "Fallback AP start failed"},
    {APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED, "wifi_ap_client_connected", "AP client connected"},
    {APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED, "wifi_ap_client_disconnected", "AP client disconnected"},
};

static app_event_metadata_t s_wifi_event_metadata[WIFI_EVENT_METADATA_SLOTS];
static size_t s_wifi_event_metadata_next = 0U;

static const wifi_event_descriptor_t *wifi_find_descriptor(app_event_id_t id)
{
    for (size_t i = 0; i < sizeof(s_wifi_event_descriptors) / sizeof(s_wifi_event_descriptors[0]); ++i) {
        if (s_wifi_event_descriptors[i].id == id) {
            return &s_wifi_event_descriptors[i];
        }
    }
    return NULL;
}

#ifdef ESP_PLATFORM
static uint64_t wifi_timestamp_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}
#else
static uint64_t wifi_timestamp_ms(void)
{
    return 0ULL;
}
#endif

static app_event_metadata_t *wifi_prepare_metadata(app_event_id_t id)
{
    size_t slot = s_wifi_event_metadata_next++;
    if (s_wifi_event_metadata_next >= WIFI_EVENT_METADATA_SLOTS) {
        s_wifi_event_metadata_next = 0U;
    }

    app_event_metadata_t *metadata = &s_wifi_event_metadata[slot];
    const wifi_event_descriptor_t *descriptor = wifi_find_descriptor(id);

    metadata->event_id = id;
    metadata->key = (descriptor != NULL && descriptor->key != NULL) ? descriptor->key : "wifi_event";
    metadata->type = "wifi";
    metadata->label = (descriptor != NULL && descriptor->label != NULL) ? descriptor->label : "Wi-Fi event";
    metadata->timestamp_ms = wifi_timestamp_ms();

    return metadata;
}

void wifi_events_set_publisher(wifi_shared_state_t *state, event_bus_publish_fn_t publisher)
{
    if (state == NULL) {
        return;
    }

    state->publisher = publisher;
}

void wifi_events_publish(wifi_shared_state_t *state, app_event_id_t id)
{
    if (state == NULL || state->publisher == NULL) {
        return;
    }

#ifdef ESP_PLATFORM
    app_event_metadata_t *metadata = wifi_prepare_metadata(id);
#else
    app_event_metadata_t *metadata = NULL;
#endif

    event_bus_event_t event = {
        .id = id,
        .payload = metadata,
        .payload_size = (metadata != NULL) ? sizeof(*metadata) : 0U,
    };

    if (!state->publisher(&event, pdMS_TO_TICKS(25))) {
        ESP_LOGW("wifi", "Failed to publish Wi-Fi event 0x%08" PRIx32, (uint32_t)id);
    }
}
