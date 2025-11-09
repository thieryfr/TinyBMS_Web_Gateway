#include "unity.h"

#include "monitoring.h"
#include "app_events.h"
#include "uart_bms.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static size_t count_json_array_entries(const char *array_token)
{
    if (array_token == NULL) {
        return 0U;
    }

    const char *start = strchr(array_token, '[');
    if (start == NULL) {
        return 0U;
    }
    const char *end = strchr(start, ']');
    if (end == NULL || end <= start) {
        return 0U;
    }

    size_t count = 0U;
    bool in_number = false;
    for (const char *p = start + 1; p < end; ++p) {
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            if (!in_number) {
                ++count;
                in_number = true;
            }
        } else if (*p == ',') {
            in_number = false;
        } else {
            in_number = false;
        }
    }

    return count;
}

static bool s_publish_called = false;
static event_bus_event_t s_last_published_event = {0};

static bool monitoring_test_publish_stub(const event_bus_event_t *event, TickType_t timeout)
{
    (void)timeout;

    if (event == NULL) {
        return false;
    }

    s_publish_called = true;
    s_last_published_event = *event;
    return true;
}

TEST_CASE("monitoring_snapshot_includes_cell_arrays", "[monitoring]")
{
    char buffer[MONITORING_SNAPSHOT_MAX_SIZE];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, monitoring_get_status_json(buffer, sizeof(buffer), &length));
    TEST_ASSERT_TRUE(length < sizeof(buffer));

    buffer[length] = '\0';

    const char *voltage_section = strstr(buffer, "\"cell_voltages_mv\":[");
    TEST_ASSERT_NOT_NULL(voltage_section);
    const char *balancing_section = strstr(buffer, "\"cell_balancing\":[");
    TEST_ASSERT_NOT_NULL(balancing_section);

    TEST_ASSERT_EQUAL_UINT32(UART_BMS_CELL_COUNT, count_json_array_entries(voltage_section));
    TEST_ASSERT_EQUAL_UINT32(UART_BMS_CELL_COUNT, count_json_array_entries(balancing_section));
}

TEST_CASE("monitoring_publishes_diagnostics_snapshot", "[monitoring]")
{
    s_publish_called = false;
    memset(&s_last_published_event, 0, sizeof(s_last_published_event));

    monitoring_set_event_publisher(monitoring_test_publish_stub);

    TEST_ASSERT_EQUAL(ESP_OK, monitoring_publish_diagnostics_snapshot());
    TEST_ASSERT_TRUE(s_publish_called);
    TEST_ASSERT_EQUAL(APP_EVENT_ID_MONITORING_DIAGNOSTICS, s_last_published_event.id);
    TEST_ASSERT_NOT_NULL(s_last_published_event.payload);

    const char *payload = (const char *)s_last_published_event.payload;
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"type\":\"monitoring_diagnostics\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"mutex_timeouts\""));

    monitoring_set_event_publisher(NULL);
}
