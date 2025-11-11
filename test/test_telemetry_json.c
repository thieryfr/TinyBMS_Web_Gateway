#include "unity.h"

#include "telemetry_json.h"
#include "system_boot_counter.h"

#include "cJSON.h"
#include "can_publisher.h"
#include "uart_bms.h"

#include <string.h>
#include <time.h>

TEST_CASE("telemetry_json_metrics_schema", "[telemetry]")
{
    uart_bms_live_data_t sample = {0};
    sample.timestamp_ms = 123456U;
    sample.uptime_seconds = 77U;
    sample.cycle_count = 12U;
    sample.pack_voltage_v = 52.8f;
    sample.pack_current_a = -13.5f;
    sample.state_of_charge_pct = 76.4f;
    sample.state_of_health_pct = 97.2f;
    sample.average_temperature_c = 32.1f;
    sample.mosfet_temperature_c = 35.2f;
    sample.min_cell_mv = 3300U;
    sample.max_cell_mv = 3450U;
    sample.balancing_bits = 0x0003U;
    sample.alarm_bits = 0x12U;
    sample.warning_bits = 0x34U;
    sample.max_charge_current_limit_a = 40.0f;
    sample.max_discharge_current_limit_a = 60.0f;
    sample.charge_overcurrent_limit_a = 38.0f;
    sample.discharge_overcurrent_limit_a = 10.0f;

    for (size_t i = 0; i < UART_BMS_CELL_COUNT; ++i) {
        sample.cell_voltage_mv[i] = (uint16_t)(3300U + (uint16_t)i);
        sample.cell_balancing[i] = (uint8_t)(i % 2U);
    }

    system_boot_counter_mock_set(7U);

    char buffer[1024];
    size_t length = 0U;
    TEST_ASSERT_TRUE(telemetry_json_write_metrics(&sample, buffer, sizeof(buffer), &length));
    TEST_ASSERT_GREATER_THAN(0U, length);

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_TRUE(cJSON_IsString(type));
    TEST_ASSERT_EQUAL_STRING("tinybms_metrics", type->valuestring);

    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(root, "timestamp_ms");
    TEST_ASSERT_TRUE(cJSON_IsNumber(timestamp));
    TEST_ASSERT_EQUAL_DOUBLE(sample.timestamp_ms, timestamp->valuedouble);

    const cJSON *cell_voltages = cJSON_GetObjectItemCaseSensitive(root, "cell_voltages_mv");
    TEST_ASSERT_TRUE(cJSON_IsArray(cell_voltages));
    TEST_ASSERT_EQUAL_INT(UART_BMS_CELL_COUNT, cJSON_GetArraySize(cell_voltages));

    const cJSON *boot = cJSON_GetObjectItemCaseSensitive(root, "boot_count");
    TEST_ASSERT_TRUE(cJSON_IsNumber(boot));
    TEST_ASSERT_EQUAL_DOUBLE(7.0, boot->valuedouble);

    const cJSON *alarms = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    TEST_ASSERT_TRUE(cJSON_IsObject(alarms));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alarms, "raw_alarm_bits")));

    const cJSON *limits = cJSON_GetObjectItemCaseSensitive(root, "limits");
    TEST_ASSERT_TRUE(cJSON_IsObject(limits));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(limits, "max_discharge_current_a")));

    cJSON_Delete(root);
}

TEST_CASE("telemetry_json_can_ready_schema", "[telemetry]")
{
    can_publisher_frame_t frame = {0};
    frame.id = 0x18FF50E5U;
    frame.dlc = 4U;
    frame.timestamp_ms = 42U;
    frame.data[0] = 0xAAU;
    frame.data[1] = 0xBBU;
    frame.data[2] = 0xCCU;
    frame.data[3] = 0xDDU;

    char buffer[256];
    size_t length = 0U;
    TEST_ASSERT_TRUE(telemetry_json_write_can_ready(&frame, buffer, sizeof(buffer), &length));
    TEST_ASSERT_GREATER_THAN(0U, length);

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    TEST_ASSERT_TRUE(cJSON_IsString(type));
    TEST_ASSERT_EQUAL_STRING("can_ready", type->valuestring);

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    TEST_ASSERT_TRUE(cJSON_IsString(id));
    TEST_ASSERT_EQUAL_STRING("18FF50E5", id->valuestring);

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    TEST_ASSERT_TRUE(cJSON_IsString(data));
    TEST_ASSERT_EQUAL_INT(frame.dlc * 2, (int)strlen(data->valuestring));

    cJSON_Delete(root);
}

TEST_CASE("telemetry_json_history_schema", "[telemetry]")
{
    uart_bms_live_data_t sample = {0};
    sample.timestamp_ms = 9001U;
    sample.pack_voltage_v = 48.5f;
    sample.pack_current_a = -5.25f;
    sample.state_of_charge_pct = 64.0f;
    sample.state_of_health_pct = 95.0f;
    sample.average_temperature_c = 29.5f;

    time_t now = 1700000000;
    char expected_iso[32];
    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    strftime(expected_iso, sizeof(expected_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_now);

    char buffer[256];
    size_t length = 0U;

    system_boot_counter_mock_set(21U);
    TEST_ASSERT_TRUE(telemetry_json_write_history_sample(&sample, now, buffer, sizeof(buffer), &length));

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    TEST_ASSERT_TRUE(cJSON_IsString(type));
    TEST_ASSERT_EQUAL_STRING("history_sample", type->valuestring);

    const cJSON *iso = cJSON_GetObjectItemCaseSensitive(root, "timestamp_iso");
    TEST_ASSERT_TRUE(cJSON_IsString(iso));
    TEST_ASSERT_EQUAL_STRING(expected_iso, iso->valuestring);

    const cJSON *pack_voltage = cJSON_GetObjectItemCaseSensitive(root, "pack_voltage_v");
    TEST_ASSERT_TRUE(cJSON_IsNumber(pack_voltage));
    TEST_ASSERT_EQUAL_DOUBLE(sample.pack_voltage_v, pack_voltage->valuedouble);

    const cJSON *boot = cJSON_GetObjectItemCaseSensitive(root, "boot_count");
    TEST_ASSERT_TRUE(cJSON_IsNumber(boot));
    TEST_ASSERT_EQUAL_DOUBLE(21.0, boot->valuedouble);

    cJSON_Delete(root);
}

