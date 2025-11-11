#include "unity.h"

#include "app_events.h"
#include "event_bus.h"
#include "wifi_events.h"
#include "wifi_state.h"
#include "wifi_state_machine.h"

#ifndef CONFIG_TINYBMS_WIFI_AP_FALLBACK
#define CONFIG_TINYBMS_WIFI_AP_FALLBACK 1
#endif

static wifi_shared_state_t s_state;

static struct {
    app_event_id_t ids[16];
    size_t count;
} s_event_log;

static bool test_publish(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;
    if (s_event_log.count < (sizeof(s_event_log.ids) / sizeof(s_event_log.ids[0]))) {
        s_event_log.ids[s_event_log.count++] = event->id;
    }
    return true;
}

void setUp(void)
{
    s_event_log.count = 0;
    wifi_state_machine_init(&s_state);
    wifi_events_set_publisher(&s_state, test_publish);
}

void tearDown(void)
{
    wifi_state_machine_deinit(&s_state);
    wifi_state_clear_publisher(&s_state);
}

static void assert_event_published(app_event_id_t expected)
{
    TEST_ASSERT_TRUE_MESSAGE(s_event_log.count > 0, "Expected at least one event");
    TEST_ASSERT_EQUAL_UINT32(expected, s_event_log.ids[s_event_log.count - 1]);
}

void test_wifi_state_machine_publishes_sta_sequence(void)
{
    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_STA_START, NULL);
    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_STA_CONNECTED, NULL);
    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_STA_GOT_IP, NULL);

    TEST_ASSERT_EQUAL_size_t(3, s_event_log.count);
    TEST_ASSERT_EQUAL_UINT32(APP_EVENT_ID_WIFI_STA_START, s_event_log.ids[0]);
    TEST_ASSERT_EQUAL_UINT32(APP_EVENT_ID_WIFI_STA_CONNECTED, s_event_log.ids[1]);
    TEST_ASSERT_EQUAL_UINT32(APP_EVENT_ID_WIFI_STA_GOT_IP, s_event_log.ids[2]);
}

void test_wifi_state_machine_reports_ap_activity(void)
{
    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_AP_STARTED, NULL);
    assert_event_published(APP_EVENT_ID_WIFI_AP_STARTED);

    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_AP_CLIENT_CONNECTED, NULL);
    assert_event_published(APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED);

    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_AP_CLIENT_DISCONNECTED, NULL);
    assert_event_published(APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED);

    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_AP_STOPPED, NULL);
    assert_event_published(APP_EVENT_ID_WIFI_AP_STOPPED);
}

void test_wifi_state_machine_handles_sta_disconnection_event(void)
{
    wifi_state_disconnected_info_t info = {.reason = 42};
    wifi_state_machine_process_transition(&s_state, WIFI_STATE_TRANSITION_STA_DISCONNECTED, &info);

    TEST_ASSERT_EQUAL_size_t(1, s_event_log.count);
    TEST_ASSERT_EQUAL_UINT32(APP_EVENT_ID_WIFI_STA_DISCONNECTED, s_event_log.ids[0]);
}
