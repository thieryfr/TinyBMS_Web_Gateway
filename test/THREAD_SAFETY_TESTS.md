# Thread Safety Unit Tests

## Overview

The `test_thread_safety.c` test suite validates that mutex-protected modules can handle concurrent access from multiple FreeRTOS tasks without data corruption or race conditions.

## Test Coverage

### 1. config_manager Module

**Test: `config_manager_concurrent_setters`**
- Launches 4 concurrent tasks
- Each task performs 50 setter operations (`config_manager_set_uart_poll_interval_ms()`)
- Verifies all operations complete successfully without errors
- Validates mutex prevents data corruption

**Test: `config_manager_mixed_read_write`**
- Launches 4 concurrent tasks with mixed read/write operations
- Alternates between setters and getters
- Verifies reads return valid values (no corruption from concurrent writes)
- Validates consistency under read/write contention

### 2. monitoring Module

**Test: `monitoring_concurrent_status_reads`**
- Launches 4 concurrent tasks
- Each task performs 50 status read operations (`monitoring_get_status_json()`)
- Verifies all operations complete successfully
- Validates JSON output integrity (starts with '{')
- Ensures mutex prevents torn reads

**Test: `monitoring_concurrent_history_reads`**
- Launches 4 concurrent tasks
- Each task performs 50 history read operations (`monitoring_get_history_json()`)
- Verifies all operations complete successfully
- Validates JSON output integrity (starts with '[')
- Ensures mutex prevents buffer corruption

### 3. General Thread Safety

**Test: `mutex_timeout_behavior`**
- Validates mutex operations don't deadlock
- Performs 100 sequential operations
- Verifies completion within reasonable time (<1 second)
- Ensures timeout mechanisms work correctly

**Test: `data_consistency_high_contention`**
- Stress test with 8 concurrent tasks
- Each task performs 50 write operations
- Verifies final state is consistent (no corruption)
- Validates system behavior under high contention

## Test Architecture

### Test Parameters
```c
#define TEST_THREAD_COUNT 4              // Number of concurrent tasks
#define TEST_ITERATIONS_PER_THREAD 50    // Operations per task
#define TEST_TIMEOUT_MS 5000             // Max completion time
```

### Synchronization Pattern

All tests use a common synchronization pattern:

1. **Initialization**
   - Reset shared state (`s_test_failed`, `s_completed_tasks`)
   - Create completion tracking mutex

2. **Task Launch**
   - Launch N concurrent FreeRTOS tasks
   - Each task runs TEST_ITERATIONS_PER_THREAD iterations

3. **Completion Tracking**
   - Tasks call `mark_task_completed()` when done
   - Main test uses `wait_for_completion()` to wait with timeout

4. **Validation**
   - Verify all tasks completed within timeout
   - Check no operations failed (`s_test_failed == false`)
   - Validate data integrity

## Running the Tests

### Prerequisites
- ESP-IDF v5.x development environment
- Unity test framework (included with ESP-IDF)

### Build and Run
```bash
# Configure project for testing
idf.py menuconfig
# Navigate to: Component config → Unity test framework
# Enable desired test options

# Build tests
idf.py build

# Flash and run tests
idf.py flash monitor

# Run specific test suite
idf.py flash monitor -p /dev/ttyUSB0
# In monitor, type: ![thread_safety]
```

### Test Tags
- `[thread_safety]` - All thread safety tests
- `[config_manager]` - config_manager specific tests
- `[monitoring]` - monitoring specific tests
- `[general]` - General mutex behavior tests
- `[stress]` - High contention stress tests

## Expected Behavior

### Success Criteria
✅ All tasks complete within timeout (5 seconds)
✅ No operations fail (`s_test_failed == false`)
✅ JSON outputs are well-formed
✅ Final data state is consistent
✅ No mutex deadlocks occur

### Failure Modes

**Timeout Failure**
- Indicates potential deadlock
- Check mutex acquisition/release balance
- Verify timeout values are reasonable

**Operation Failure**
- Indicates ESP_ERR_TIMEOUT from mutex operations
- Check mutex hold times
- Verify no priority inversion issues

**Data Corruption**
- Invalid JSON output (wrong start character)
- Out-of-range values
- Indicates race condition or missing mutex protection

## Module-Specific Thread Safety

### config_manager
- **Protected Resource**: `s_config_mutex`
- **Functions Tested**: `config_manager_set_uart_poll_interval_ms()`
- **Concurrency**: Multiple setters from different tasks
- **Timeout**: 1000ms (includes NVS write time)

### monitoring
- **Protected Resource**: `s_monitoring_mutex`
- **Functions Tested**:
  - `monitoring_get_status_json()`
  - `monitoring_get_history_json()`
- **Concurrency**: Multiple readers from web/MQTT/UART tasks
- **Timeout**: 100ms

## Future Enhancements

Potential additions to the test suite:

1. **can_publisher Tests**
   - Concurrent publish operations
   - Event listener registration/unregister during publish

2. **conversion_table Tests**
   - Concurrent energy counter updates
   - Mixed read/write on energy values

3. **can_victron Tests**
   - Concurrent driver state queries
   - Start/stop during active queries

4. **Priority Inversion Tests**
   - Launch tasks at different priorities
   - Verify priority inheritance works correctly

5. **Long-Duration Stress Tests**
   - Run for extended periods (minutes/hours)
   - Detect rare race conditions

## Troubleshooting

### Test Hangs
- Check for deadlocks in mutex acquisition
- Verify all code paths release mutexes
- Look for priority inversion

### Intermittent Failures
- Increase TEST_TIMEOUT_MS
- Reduce TEST_THREAD_COUNT or TEST_ITERATIONS_PER_THREAD
- Check for timing-dependent bugs

### Memory Issues
- Monitor stack usage with `uxTaskGetStackHighWaterMark()`
- Increase task stack size if needed (currently 4096 bytes)
- Check for heap fragmentation

## References

- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)
- [ESP-IDF Unity Testing](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
- [FreeRTOS Mutex](https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html)

## Maintenance

When modifying thread-safe modules:

1. **Add Mutex Protection**
   - Update module implementation with mutex
   - Document in module header's `@section thread_safety`

2. **Add Unit Test**
   - Add test case to `test_thread_safety.c`
   - Follow existing pattern (concurrent tasks + validation)
   - Use appropriate test tags

3. **Verify Coverage**
   - Run tests before merging
   - Check all concurrent scenarios are covered
   - Update this documentation
