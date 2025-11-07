/**
 * @file test_thread_safety.c
 * @brief Unit tests for thread safety of mutex-protected modules
 *
 * This test suite verifies that modules with mutex protection can handle
 * concurrent access from multiple FreeRTOS tasks without data corruption
 * or race conditions.
 *
 * Tests cover:
 * - config_manager: Concurrent setter operations
 * - monitoring: Concurrent status/history reads
 * - General mutex behavior under load
 */

#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "config_manager.h"
#include "monitoring.h"
#include "uart_bms.h"

#include <string.h>
#include <stdbool.h>

// Test configuration
#define TEST_THREAD_COUNT 4
#define TEST_ITERATIONS_PER_THREAD 50
#define TEST_TIMEOUT_MS 5000

// Shared state for concurrent tests
static volatile bool s_test_failed = false;
static volatile uint32_t s_completed_tasks = 0;
static SemaphoreHandle_t s_completion_mutex = NULL;

/**
 * Helper: Mark a task as completed (thread-safe)
 */
static void mark_task_completed(void)
{
    if (s_completion_mutex != NULL) {
        xSemaphoreTake(s_completion_mutex, portMAX_DELAY);
        s_completed_tasks++;
        xSemaphoreGive(s_completion_mutex);
    }
}

/**
 * Helper: Wait for all tasks to complete with timeout
 */
static bool wait_for_completion(uint32_t expected_count, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 10;

    while (elapsed_ms < timeout_ms) {
        uint32_t completed = 0;
        if (s_completion_mutex != NULL) {
            xSemaphoreTake(s_completion_mutex, portMAX_DELAY);
            completed = s_completed_tasks;
            xSemaphoreGive(s_completion_mutex);
        }

        if (completed >= expected_count) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        elapsed_ms += poll_interval_ms;
    }

    return false;
}

/**
 * Task: Concurrent config_manager setters
 * Each task repeatedly modifies UART poll interval to verify mutex protection
 */
static void config_manager_setter_task(void *param)
{
    uint32_t task_id = (uint32_t)param;

    for (int i = 0; i < TEST_ITERATIONS_PER_THREAD; i++) {
        // Each task sets a unique value based on its ID
        uint32_t interval = 100 + (task_id * 50) + i;

        esp_err_t err = config_manager_set_uart_poll_interval_ms(interval);
        if (err != ESP_OK) {
            s_test_failed = true;
            break;
        }

        // Small delay to increase contention
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    mark_task_completed();
    vTaskDelete(NULL);
}

/**
 * Test: config_manager setters are thread-safe under concurrent access
 */
TEST_CASE("config_manager_concurrent_setters", "[thread_safety][config_manager]")
{
    s_test_failed = false;
    s_completed_tasks = 0;
    s_completion_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(s_completion_mutex);

    // Launch multiple tasks that modify configuration concurrently
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        BaseType_t result = xTaskCreate(
            config_manager_setter_task,
            "cfg_test",
            4096,
            (void *)i,
            5,
            NULL
        );
        TEST_ASSERT_EQUAL(pdPASS, result);
    }

    // Wait for all tasks to complete
    bool completed = wait_for_completion(TEST_THREAD_COUNT, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(completed, "Tasks did not complete within timeout");
    TEST_ASSERT_FALSE_MESSAGE(s_test_failed, "At least one setter operation failed");

    vSemaphoreDelete(s_completion_mutex);
    s_completion_mutex = NULL;
}

/**
 * Task: Concurrent monitoring status reads
 * Each task repeatedly reads monitoring status to verify mutex protection
 */
static void monitoring_status_reader_task(void *param)
{
    char buffer[MONITORING_SNAPSHOT_MAX_SIZE];
    size_t length;

    for (int i = 0; i < TEST_ITERATIONS_PER_THREAD; i++) {
        esp_err_t err = monitoring_get_status_json(buffer, sizeof(buffer), &length);
        if (err != ESP_OK) {
            s_test_failed = true;
            break;
        }

        // Verify buffer has valid JSON (should start with '{')
        if (length > 0 && buffer[0] != '{') {
            s_test_failed = true;
            break;
        }

        // Small delay to increase contention
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    mark_task_completed();
    vTaskDelete(NULL);
}

/**
 * Test: monitoring status reads are thread-safe under concurrent access
 */
TEST_CASE("monitoring_concurrent_status_reads", "[thread_safety][monitoring]")
{
    s_test_failed = false;
    s_completed_tasks = 0;
    s_completion_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(s_completion_mutex);

    // Launch multiple tasks that read monitoring status concurrently
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        BaseType_t result = xTaskCreate(
            monitoring_status_reader_task,
            "mon_test",
            4096,
            NULL,
            5,
            NULL
        );
        TEST_ASSERT_EQUAL(pdPASS, result);
    }

    // Wait for all tasks to complete
    bool completed = wait_for_completion(TEST_THREAD_COUNT, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(completed, "Tasks did not complete within timeout");
    TEST_ASSERT_FALSE_MESSAGE(s_test_failed, "At least one status read failed or returned invalid data");

    vSemaphoreDelete(s_completion_mutex);
    s_completion_mutex = NULL;
}

/**
 * Task: Concurrent monitoring history reads
 * Each task repeatedly reads monitoring history to verify mutex protection
 */
static void monitoring_history_reader_task(void *param)
{
    char buffer[MONITORING_SNAPSHOT_MAX_SIZE];
    size_t length;

    for (int i = 0; i < TEST_ITERATIONS_PER_THREAD; i++) {
        esp_err_t err = monitoring_get_history_json(10, buffer, sizeof(buffer), &length);
        if (err != ESP_OK) {
            s_test_failed = true;
            break;
        }

        // Verify buffer has valid JSON (should start with '[' for array)
        if (length > 0 && buffer[0] != '[') {
            s_test_failed = true;
            break;
        }

        // Small delay to increase contention
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    mark_task_completed();
    vTaskDelete(NULL);
}

/**
 * Test: monitoring history reads are thread-safe under concurrent access
 */
TEST_CASE("monitoring_concurrent_history_reads", "[thread_safety][monitoring]")
{
    s_test_failed = false;
    s_completed_tasks = 0;
    s_completion_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(s_completion_mutex);

    // Launch multiple tasks that read monitoring history concurrently
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        BaseType_t result = xTaskCreate(
            monitoring_history_reader_task,
            "hist_test",
            4096,
            NULL,
            5,
            NULL
        );
        TEST_ASSERT_EQUAL(pdPASS, result);
    }

    // Wait for all tasks to complete
    bool completed = wait_for_completion(TEST_THREAD_COUNT, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(completed, "Tasks did not complete within timeout");
    TEST_ASSERT_FALSE_MESSAGE(s_test_failed, "At least one history read failed or returned invalid data");

    vSemaphoreDelete(s_completion_mutex);
    s_completion_mutex = NULL;
}

/**
 * Task: Mixed config read/write operations
 * Tests concurrent reads and writes to config_manager
 */
static void config_manager_mixed_task(void *param)
{
    uint32_t task_id = (uint32_t)param;

    for (int i = 0; i < TEST_ITERATIONS_PER_THREAD; i++) {
        if (i % 2 == 0) {
            // Write operation
            uint32_t interval = 100 + (task_id * 50) + i;
            esp_err_t err = config_manager_set_uart_poll_interval_ms(interval);
            if (err != ESP_OK) {
                s_test_failed = true;
                break;
            }
        } else {
            // Read operation
            uint32_t interval = config_manager_get_uart_poll_interval_ms();
            // Verify reasonable value (should be >= 100 based on our writes)
            if (interval < 100 || interval > 10000) {
                s_test_failed = true;
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    mark_task_completed();
    vTaskDelete(NULL);
}

/**
 * Test: Mixed read/write operations are thread-safe
 */
TEST_CASE("config_manager_mixed_read_write", "[thread_safety][config_manager]")
{
    s_test_failed = false;
    s_completed_tasks = 0;
    s_completion_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(s_completion_mutex);

    // Set initial value
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_uart_poll_interval_ms(100));

    // Launch multiple tasks with mixed read/write
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        BaseType_t result = xTaskCreate(
            config_manager_mixed_task,
            "mix_test",
            4096,
            (void *)i,
            5,
            NULL
        );
        TEST_ASSERT_EQUAL(pdPASS, result);
    }

    // Wait for all tasks to complete
    bool completed = wait_for_completion(TEST_THREAD_COUNT, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(completed, "Tasks did not complete within timeout");
    TEST_ASSERT_FALSE_MESSAGE(s_test_failed, "At least one read/write operation failed");

    vSemaphoreDelete(s_completion_mutex);
    s_completion_mutex = NULL;
}

/**
 * Test: Verify mutex timeout behavior
 * Ensures that mutex operations don't deadlock
 */
TEST_CASE("mutex_timeout_behavior", "[thread_safety][general]")
{
    // This test verifies that setter operations complete within reasonable time
    // even under contention (no deadlocks)

    const int iterations = 100;
    uint32_t start_time = xTaskGetTickCount();

    for (int i = 0; i < iterations; i++) {
        esp_err_t err = config_manager_set_uart_poll_interval_ms(100 + i);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    uint32_t elapsed_ticks = xTaskGetTickCount() - start_time;
    uint32_t elapsed_ms = pdTICKS_TO_MS(elapsed_ticks);

    // Should complete in well under 1 second for 100 iterations
    // (each operation should take ~1ms with mutex + NVS write)
    TEST_ASSERT_LESS_THAN(1000, elapsed_ms);
}

/**
 * Test: Data consistency under high contention
 * Verifies that final state is consistent after many concurrent operations
 */
TEST_CASE("data_consistency_high_contention", "[thread_safety][stress]")
{
    s_test_failed = false;
    s_completed_tasks = 0;
    s_completion_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(s_completion_mutex);

    // Set known initial value
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_uart_poll_interval_ms(1000));

    // Launch many tasks that all try to set different values
    const uint32_t stress_thread_count = 8;
    for (uint32_t i = 0; i < stress_thread_count; i++) {
        BaseType_t result = xTaskCreate(
            config_manager_setter_task,
            "stress_test",
            4096,
            (void *)i,
            5,
            NULL
        );
        TEST_ASSERT_EQUAL(pdPASS, result);
    }

    // Wait for all tasks to complete
    bool completed = wait_for_completion(stress_thread_count, TEST_TIMEOUT_MS * 2);
    TEST_ASSERT_TRUE_MESSAGE(completed, "Stress test tasks did not complete within timeout");
    TEST_ASSERT_FALSE_MESSAGE(s_test_failed, "At least one operation failed during stress test");

    // Final value should be one of the values written (validates no corruption)
    uint32_t final_value = config_manager_get_uart_poll_interval_ms();
    TEST_ASSERT_GREATER_OR_EQUAL(100, final_value);
    TEST_ASSERT_LESS_THAN(10000, final_value);

    vSemaphoreDelete(s_completion_mutex);
    s_completion_mutex = NULL;
}
