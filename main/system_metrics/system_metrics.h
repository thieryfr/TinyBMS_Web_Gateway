#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
#define SYSTEM_METRICS_MAX_CORES 1U
#else
#define SYSTEM_METRICS_MAX_CORES 2U
#endif

#define SYSTEM_METRICS_MAX_NAME_LENGTH      32U
#define SYSTEM_METRICS_MAX_CONSUMERS        16U
#define SYSTEM_METRICS_MAX_TASKS            32U
#define SYSTEM_METRICS_MAX_MODULES          SYSTEM_METRICS_MAX_CONSUMERS
#define SYSTEM_METRICS_MAX_DETAIL_LENGTH    96U
#define SYSTEM_METRICS_MAX_FIRMWARE_LENGTH  64U
#define SYSTEM_METRICS_MAX_TIMESTAMP_LENGTH 32U

typedef struct {
    uint64_t timestamp_ms;
    uint32_t uptime_s;
    uint32_t boot_count;
    uint32_t cycle_count;
    esp_reset_reason_t reset_reason;
    char reset_reason_str[SYSTEM_METRICS_MAX_NAME_LENGTH];
    char firmware[SYSTEM_METRICS_MAX_FIRMWARE_LENGTH];
    char last_boot_iso[SYSTEM_METRICS_MAX_TIMESTAMP_LENGTH];
    uint32_t total_heap_bytes;
    uint32_t free_heap_bytes;
    uint32_t min_free_heap_bytes;
    float cpu_load_percent[SYSTEM_METRICS_MAX_CORES];
    size_t cpu_load_count;
    float event_loop_avg_latency_ms;
    float event_loop_max_latency_ms;
} system_metrics_runtime_t;

typedef struct {
    char name[SYSTEM_METRICS_MAX_NAME_LENGTH];
    uint32_t dropped_events;
    uint32_t queue_capacity;
    uint32_t messages_waiting;
} system_metrics_event_bus_consumer_t;

typedef struct {
    uint32_t dropped_total;
    size_t consumer_count;
    system_metrics_event_bus_consumer_t consumers[SYSTEM_METRICS_MAX_CONSUMERS];
} system_metrics_event_bus_metrics_t;

typedef struct {
    char name[SYSTEM_METRICS_MAX_NAME_LENGTH];
    float cpu_percent;
    uint32_t runtime_ticks;
    uint32_t stack_high_water_mark;
    int32_t core_id;
    uint32_t state;
} system_metrics_task_info_t;

typedef struct {
    size_t task_count;
    system_metrics_task_info_t tasks[SYSTEM_METRICS_MAX_TASKS];
} system_metrics_task_snapshot_t;

typedef enum {
    SYSTEM_METRICS_MODULE_STATUS_OK = 0,
    SYSTEM_METRICS_MODULE_STATUS_WARNING = 1,
    SYSTEM_METRICS_MODULE_STATUS_ERROR = 2,
} system_metrics_module_status_t;

typedef struct {
    char name[SYSTEM_METRICS_MAX_NAME_LENGTH];
    system_metrics_module_status_t status;
    char detail[SYSTEM_METRICS_MAX_DETAIL_LENGTH];
    char last_event_iso[SYSTEM_METRICS_MAX_TIMESTAMP_LENGTH];
} system_metrics_module_info_t;

typedef struct {
    size_t module_count;
    system_metrics_module_info_t modules[SYSTEM_METRICS_MAX_MODULES];
} system_metrics_module_snapshot_t;

esp_err_t system_metrics_collect_runtime(system_metrics_runtime_t *out_runtime);
esp_err_t system_metrics_collect_event_bus(system_metrics_event_bus_metrics_t *out_metrics);
esp_err_t system_metrics_collect_tasks(system_metrics_task_snapshot_t *out_tasks);
esp_err_t system_metrics_collect_modules(system_metrics_module_snapshot_t *out_modules,
                                         const system_metrics_event_bus_metrics_t *event_bus_metrics);

esp_err_t system_metrics_runtime_to_json(const system_metrics_runtime_t *runtime,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length);

esp_err_t system_metrics_event_bus_to_json(const system_metrics_event_bus_metrics_t *metrics,
                                           char *buffer,
                                           size_t buffer_size,
                                           size_t *out_length);

esp_err_t system_metrics_tasks_to_json(const system_metrics_task_snapshot_t *tasks,
                                       char *buffer,
                                       size_t buffer_size,
                                       size_t *out_length);

esp_err_t system_metrics_modules_to_json(const system_metrics_module_snapshot_t *modules,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length);

#ifdef __cplusplus
}
#endif

