#include "unity.h"

#include "system_metrics.h"

#include "cJSON.h"

#include <string.h>

TEST_CASE("system_metrics_runtime_to_json_serializes_fields", "[system_metrics]")
{
    system_metrics_runtime_t runtime = {
        .timestamp_ms = 123456789ULL,
        .uptime_s = 3600,
        .boot_count = 2,
        .cycle_count = 42,
        .total_heap_bytes = 512000,
        .free_heap_bytes = 256000,
        .min_free_heap_bytes = 192000,
        .cpu_load_count = SYSTEM_METRICS_MAX_CORES,
        .event_loop_avg_latency_ms = 1.5f,
        .event_loop_max_latency_ms = 3.5f,
    };
    strncpy(runtime.reset_reason_str, "ESP_RST_SW", sizeof(runtime.reset_reason_str) - 1U);
    strncpy(runtime.firmware, "test-fw", sizeof(runtime.firmware) - 1U);
    strncpy(runtime.last_boot_iso, "2024-01-01T00:00:00Z", sizeof(runtime.last_boot_iso) - 1U);
    for (size_t i = 0; i < runtime.cpu_load_count; ++i) {
        runtime.cpu_load_percent[i] = 25.0f * (float)(i + 1U);
    }

    char buffer[1024];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, system_metrics_runtime_to_json(&runtime, buffer, sizeof(buffer), &length));

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL(3600, cJSON_GetObjectItem(root, "uptime_s")->valueint);
    TEST_ASSERT_EQUAL_STRING("test-fw", cJSON_GetObjectItem(root, "firmware")->valuestring);
    cJSON *cpu = cJSON_GetObjectItem(root, "cpu_load");
    TEST_ASSERT_NOT_NULL(cpu);
    cJSON *core0 = cJSON_GetObjectItem(cpu, "core0");
    TEST_ASSERT_NOT_NULL(core0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 25.0, core0->valuedouble);

    cJSON_Delete(root);
}

TEST_CASE("system_metrics_event_bus_to_json_lists_consumers", "[system_metrics]")
{
    system_metrics_event_bus_metrics_t metrics = {
        .dropped_total = 5,
        .consumer_count = 2,
    };
    strncpy(metrics.consumers[0].name, "web_server", sizeof(metrics.consumers[0].name) - 1U);
    metrics.consumers[0].dropped_events = 3;
    metrics.consumers[0].queue_capacity = 32;
    metrics.consumers[0].messages_waiting = 4;

    strncpy(metrics.consumers[1].name, "mqtt", sizeof(metrics.consumers[1].name) - 1U);
    metrics.consumers[1].dropped_events = 2;
    metrics.consumers[1].queue_capacity = 16;
    metrics.consumers[1].messages_waiting = 1;

    char buffer[1024];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, system_metrics_event_bus_to_json(&metrics, buffer, sizeof(buffer), &length));

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(5, cJSON_GetObjectItem(root, "dropped_total")->valueint);

    cJSON *drops = cJSON_GetObjectItem(root, "dropped_by_consumer");
    TEST_ASSERT_NOT_NULL(drops);
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(drops));

    cJSON_Delete(root);
}

TEST_CASE("system_metrics_tasks_to_json_serializes_snapshot", "[system_metrics]")
{
    system_metrics_task_snapshot_t snapshot = {
        .task_count = 1,
    };
    strncpy(snapshot.tasks[0].name, "main", sizeof(snapshot.tasks[0].name) - 1U);
    snapshot.tasks[0].state = eRunning;
    snapshot.tasks[0].cpu_percent = 12.5f;
    snapshot.tasks[0].runtime_ticks = 1000;
    snapshot.tasks[0].stack_high_water_mark = 2048;
    snapshot.tasks[0].core_id = 0;

    char buffer[1024];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, system_metrics_tasks_to_json(&snapshot, buffer, sizeof(buffer), &length));

    cJSON *array = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(array);
    TEST_ASSERT_TRUE(cJSON_IsArray(array));
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(array));

    cJSON *entry = cJSON_GetArrayItem(array, 0);
    TEST_ASSERT_EQUAL_STRING("main", cJSON_GetObjectItem(entry, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("running", cJSON_GetObjectItem(entry, "state")->valuestring);

    cJSON_Delete(array);
}

TEST_CASE("system_metrics_modules_to_json_serializes_modules", "[system_metrics]")
{
    system_metrics_module_snapshot_t modules = {
        .module_count = 1,
    };
    strncpy(modules.modules[0].name, "event_bus", sizeof(modules.modules[0].name) - 1U);
    modules.modules[0].status = SYSTEM_METRICS_MODULE_STATUS_OK;
    strncpy(modules.modules[0].detail, "Queue 1/32", sizeof(modules.modules[0].detail) - 1U);
    strncpy(modules.modules[0].last_event_iso, "", sizeof(modules.modules[0].last_event_iso));

    char buffer[1024];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, system_metrics_modules_to_json(&modules, buffer, sizeof(buffer), &length));

    cJSON *array = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(array);
    TEST_ASSERT_TRUE(cJSON_IsArray(array));

    cJSON *entry = cJSON_GetArrayItem(array, 0);
    TEST_ASSERT_EQUAL_STRING("event_bus", cJSON_GetObjectItem(entry, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("ok", cJSON_GetObjectItem(entry, "status")->valuestring);

    cJSON_Delete(array);
}

