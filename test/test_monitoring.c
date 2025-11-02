#include "unity.h"

#include "monitoring.h"
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
