#include "system_metrics.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "event_bus.h"

#define TAG "sys_metrics"

static const char *system_metrics_reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "ESP_RST_POWERON";
    case ESP_RST_EXT:
        return "ESP_RST_EXT";
    case ESP_RST_SW:
        return "ESP_RST_SW";
    case ESP_RST_PANIC:
        return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT:
        return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT:
        return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT:
        return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP:
        return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO:
        return "ESP_RST_SDIO";
    case ESP_RST_USB:
        return "ESP_RST_USB";
    case ESP_RST_USB_BROWNOUT:
        return "ESP_RST_USB_BROWNOUT";
    default:
        return "ESP_RST_UNKNOWN";
    }
}

static void system_metrics_populate_last_boot(system_metrics_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    time_t now = 0;
    if (time(&now) == (time_t)-1 || now <= 0) {
        runtime->last_boot_iso[0] = '\0';
        return;
    }

    if (runtime->uptime_s >= (uint32_t)now) {
        runtime->last_boot_iso[0] = '\0';
        return;
    }

    time_t boot_time = now - (time_t)runtime->uptime_s;
    struct tm boot_tm;
    if (gmtime_r(&boot_time, &boot_tm) == NULL) {
        runtime->last_boot_iso[0] = '\0';
        return;
    }

    if (strftime(runtime->last_boot_iso,
                 sizeof(runtime->last_boot_iso),
                 "%Y-%m-%dT%H:%M:%SZ",
                 &boot_tm) == 0) {
        runtime->last_boot_iso[0] = '\0';
    }
}

static void system_metrics_populate_firmware(system_metrics_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    const esp_app_desc_t *desc = esp_ota_get_app_description();
    if (desc != NULL && desc->version[0] != '\0') {
        strncpy(runtime->firmware, desc->version, sizeof(runtime->firmware) - 1U);
        runtime->firmware[sizeof(runtime->firmware) - 1U] = '\0';
    } else {
        strncpy(runtime->firmware, esp_get_idf_version(), sizeof(runtime->firmware) - 1U);
        runtime->firmware[sizeof(runtime->firmware) - 1U] = '\0';
    }
}

static void system_metrics_compute_cpu_load(system_metrics_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1) && \
    defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0) {
        runtime->cpu_load_count = 0;
        return;
    }

    TaskStatus_t *statuses = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (statuses == NULL) {
        runtime->cpu_load_count = 0;
        ESP_LOGW(TAG, "Unable to allocate buffer for task runtime stats");
        return;
    }

    uint32_t total_runtime = 0;
    UBaseType_t collected = uxTaskGetSystemState(statuses, task_count, &total_runtime);
    if (collected == 0 || total_runtime == 0) {
        vPortFree(statuses);
        runtime->cpu_load_count = 0;
        return;
    }

    runtime->cpu_load_count = SYSTEM_METRICS_MAX_CORES;
    for (size_t i = 0; i < runtime->cpu_load_count; ++i) {
        runtime->cpu_load_percent[i] = 0.0f;
    }

    float idle_runtime_per_core[SYSTEM_METRICS_MAX_CORES] = {0};

    for (UBaseType_t i = 0; i < collected; ++i) {
        TaskStatus_t *status = &statuses[i];
#if (SYSTEM_METRICS_MAX_CORES > 1)
        BaseType_t core_id = status->xCoreID;
        if (core_id < 0 || core_id >= (BaseType_t)SYSTEM_METRICS_MAX_CORES) {
            core_id = 0;
        }
#else
        BaseType_t core_id = 0;
#endif

        if (status->xHandle == xTaskGetIdleTaskHandleForCPU(core_id)) {
            idle_runtime_per_core[core_id] = (float)status->ulRunTimeCounter;
        }
    }

    for (size_t core = 0; core < runtime->cpu_load_count; ++core) {
        float idle_ticks = idle_runtime_per_core[core];
        float core_total = (float)total_runtime / (float)runtime->cpu_load_count;
        if (core_total <= 0.0f) {
            runtime->cpu_load_percent[core] = 0.0f;
        } else {
            float idle_pct = (idle_ticks / core_total) * 100.0f;
            if (idle_pct < 0.0f) {
                idle_pct = 0.0f;
            }
            if (idle_pct > 100.0f) {
                idle_pct = 100.0f;
            }
            runtime->cpu_load_percent[core] = 100.0f - idle_pct;
        }
    }

    vPortFree(statuses);
#else
    runtime->cpu_load_count = 0;
#endif
}

esp_err_t system_metrics_collect_runtime(system_metrics_runtime_t *out_runtime)
{
    if (out_runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_runtime, 0, sizeof(*out_runtime));

    uint64_t timestamp_us = esp_timer_get_time();
    out_runtime->timestamp_ms = timestamp_us / 1000ULL;
    out_runtime->uptime_s = (uint32_t)(timestamp_us / 1000000ULL);

    out_runtime->boot_count = 1U;
    out_runtime->cycle_count = 0U;

    out_runtime->reset_reason = esp_reset_reason();
    const char *reason = system_metrics_reset_reason_to_string(out_runtime->reset_reason);
    strncpy(out_runtime->reset_reason_str, reason, sizeof(out_runtime->reset_reason_str) - 1U);
    out_runtime->reset_reason_str[sizeof(out_runtime->reset_reason_str) - 1U] = '\0';

    out_runtime->free_heap_bytes = esp_get_free_heap_size();
    out_runtime->min_free_heap_bytes = esp_get_minimum_free_heap_size();
    out_runtime->total_heap_bytes = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    system_metrics_populate_firmware(out_runtime);
    system_metrics_populate_last_boot(out_runtime);
    system_metrics_compute_cpu_load(out_runtime);

    out_runtime->event_loop_avg_latency_ms = 0.0f;
    out_runtime->event_loop_max_latency_ms = 0.0f;

    return ESP_OK;
}

esp_err_t system_metrics_collect_event_bus(system_metrics_event_bus_metrics_t *out_metrics)
{
    if (out_metrics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_metrics, 0, sizeof(*out_metrics));

    event_bus_subscription_metrics_t buffer[SYSTEM_METRICS_MAX_CONSUMERS];
    size_t count = event_bus_get_all_metrics(buffer, SYSTEM_METRICS_MAX_CONSUMERS);
    out_metrics->consumer_count = count;

    for (size_t i = 0; i < count && i < SYSTEM_METRICS_MAX_CONSUMERS; ++i) {
        system_metrics_event_bus_consumer_t *dest = &out_metrics->consumers[i];
        const event_bus_subscription_metrics_t *src = &buffer[i];

        strncpy(dest->name, src->name, sizeof(dest->name) - 1U);
        dest->name[sizeof(dest->name) - 1U] = '\0';
        dest->dropped_events = src->dropped_events;
        dest->queue_capacity = src->queue_capacity;
        dest->messages_waiting = src->messages_waiting;

        out_metrics->dropped_total += src->dropped_events;
    }

    return ESP_OK;
}

esp_err_t system_metrics_collect_tasks(system_metrics_task_snapshot_t *out_tasks)
{
    if (out_tasks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_tasks, 0, sizeof(*out_tasks));

#if defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1) && \
    defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0) {
        return ESP_OK;
    }

    if (task_count > SYSTEM_METRICS_MAX_TASKS) {
        task_count = SYSTEM_METRICS_MAX_TASKS;
    }

    TaskStatus_t statuses[SYSTEM_METRICS_MAX_TASKS];
    uint32_t total_runtime = 0;
    UBaseType_t collected = uxTaskGetSystemState(statuses, task_count, &total_runtime);

    if (collected == 0 || total_runtime == 0) {
        return ESP_OK;
    }

    for (UBaseType_t i = 0; i < collected && i < SYSTEM_METRICS_MAX_TASKS; ++i) {
        const TaskStatus_t *status = &statuses[i];
        system_metrics_task_info_t *dest = &out_tasks->tasks[out_tasks->task_count];
        strncpy(dest->name, status->pcTaskName, sizeof(dest->name) - 1U);
        dest->name[sizeof(dest->name) - 1U] = '\0';
        dest->runtime_ticks = status->ulRunTimeCounter;
        dest->stack_high_water_mark = status->usStackHighWaterMark;
        dest->state = (uint32_t)status->eCurrentState;
#if (SYSTEM_METRICS_MAX_CORES > 1)
        dest->core_id = status->xCoreID;
#else
        dest->core_id = 0;
#endif
        dest->cpu_percent = ((float)status->ulRunTimeCounter / (float)total_runtime) * 100.0f;
        out_tasks->task_count++;
    }

    return ESP_OK;
#else
    return ESP_OK;
#endif
}

static const char *system_metrics_module_status_to_string(system_metrics_module_status_t status)
{
    switch (status) {
    case SYSTEM_METRICS_MODULE_STATUS_OK:
        return "ok";
    case SYSTEM_METRICS_MODULE_STATUS_WARNING:
        return "warning";
    case SYSTEM_METRICS_MODULE_STATUS_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

esp_err_t system_metrics_collect_modules(system_metrics_module_snapshot_t *out_modules,
                                         const system_metrics_event_bus_metrics_t *event_bus_metrics)
{
    if (out_modules == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_modules, 0, sizeof(*out_modules));

    if (event_bus_metrics == NULL) {
        return ESP_OK;
    }

    for (size_t i = 0; i < event_bus_metrics->consumer_count && i < SYSTEM_METRICS_MAX_MODULES; ++i) {
        const system_metrics_event_bus_consumer_t *consumer = &event_bus_metrics->consumers[i];
        system_metrics_module_info_t *module = &out_modules->modules[out_modules->module_count];
        strncpy(module->name, consumer->name, sizeof(module->name) - 1U);
        module->name[sizeof(module->name) - 1U] = '\0';

        if (consumer->dropped_events > 0) {
            module->status = SYSTEM_METRICS_MODULE_STATUS_WARNING;
            (void)snprintf(module->detail,
                           sizeof(module->detail),
                           "%" PRIu32 " drops depuis boot",
                           consumer->dropped_events);
        } else {
            module->status = SYSTEM_METRICS_MODULE_STATUS_OK;
            (void)snprintf(module->detail,
                           sizeof(module->detail),
                           "Queue %" PRIu32 "/%" PRIu32,
                           consumer->messages_waiting,
                           consumer->queue_capacity);
        }

        module->last_event_iso[0] = '\0';
        out_modules->module_count++;
    }

    return ESP_OK;
}

static esp_err_t system_metrics_send_json(cJSON *root, char *buffer, size_t buffer_size, size_t *out_length)
{
    if (root == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char *json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t length = strnlen(json, buffer_size + 1U);
    if (length >= buffer_size) {
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(buffer, json, length + 1U);
    cJSON_free(json);

    if (out_length != NULL) {
        *out_length = length;
    }

    return ESP_OK;
}

esp_err_t system_metrics_runtime_to_json(const system_metrics_runtime_t *runtime,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "timestamp_ms", (double)runtime->timestamp_ms);
    cJSON_AddNumberToObject(root, "uptime_s", (double)runtime->uptime_s);
    cJSON_AddNumberToObject(root, "boot_count", (double)runtime->boot_count);
    cJSON_AddNumberToObject(root, "cycle_count", (double)runtime->cycle_count);
    cJSON_AddStringToObject(root, "reset_reason", runtime->reset_reason_str);
    cJSON_AddStringToObject(root, "firmware", runtime->firmware);
    cJSON_AddStringToObject(root, "last_boot", runtime->last_boot_iso);
    cJSON_AddNumberToObject(root, "total_heap_bytes", (double)runtime->total_heap_bytes);
    cJSON_AddNumberToObject(root, "free_heap_bytes", (double)runtime->free_heap_bytes);
    cJSON_AddNumberToObject(root, "min_free_heap_bytes", (double)runtime->min_free_heap_bytes);

    cJSON *cpu_obj = cJSON_AddObjectToObject(root, "cpu_load");
    if (cpu_obj != NULL) {
        for (size_t i = 0; i < runtime->cpu_load_count; ++i) {
            char key[8];
            (void)snprintf(key, sizeof(key), "core%u", (unsigned)i);
            cJSON_AddNumberToObject(cpu_obj, key, runtime->cpu_load_percent[i]);
        }
    }

    cJSON *event_loop = cJSON_AddObjectToObject(root, "event_loop");
    if (event_loop != NULL) {
        cJSON_AddNumberToObject(event_loop, "avg_latency_ms", runtime->event_loop_avg_latency_ms);
        cJSON_AddNumberToObject(event_loop, "max_latency_ms", runtime->event_loop_max_latency_ms);
    }

    esp_err_t err = system_metrics_send_json(root, buffer, buffer_size, out_length);
    cJSON_Delete(root);
    return err;
}

esp_err_t system_metrics_event_bus_to_json(const system_metrics_event_bus_metrics_t *metrics,
                                           char *buffer,
                                           size_t buffer_size,
                                           size_t *out_length)
{
    if (metrics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "dropped_total", (double)metrics->dropped_total);

    cJSON *drops = cJSON_AddArrayToObject(root, "dropped_by_consumer");
    cJSON *queues = cJSON_AddArrayToObject(root, "queue_depth");

    for (size_t i = 0; i < metrics->consumer_count; ++i) {
        const system_metrics_event_bus_consumer_t *consumer = &metrics->consumers[i];

        if (drops != NULL) {
            cJSON *entry = cJSON_CreateObject();
            if (entry != NULL) {
                cJSON_AddStringToObject(entry, "name", consumer->name);
                cJSON_AddNumberToObject(entry, "dropped", (double)consumer->dropped_events);
                cJSON_AddItemToArray(drops, entry);
            }
        }

        if (queues != NULL) {
            cJSON *queue = cJSON_CreateObject();
            if (queue != NULL) {
                cJSON_AddStringToObject(queue, "name", consumer->name);
                cJSON_AddNumberToObject(queue, "used", (double)consumer->messages_waiting);
                cJSON_AddNumberToObject(queue, "capacity", (double)consumer->queue_capacity);
                cJSON_AddItemToArray(queues, queue);
            }
        }
    }

    esp_err_t err = system_metrics_send_json(root, buffer, buffer_size, out_length);
    cJSON_Delete(root);
    return err;
}

static const char *system_metrics_task_state_to_string(uint32_t state)
{
    switch ((eTaskState)state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

esp_err_t system_metrics_tasks_to_json(const system_metrics_task_snapshot_t *tasks,
                                       char *buffer,
                                       size_t buffer_size,
                                       size_t *out_length)
{
    if (tasks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < tasks->task_count; ++i) {
        const system_metrics_task_info_t *task = &tasks->tasks[i];
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(entry, "name", task->name);
        cJSON_AddStringToObject(entry, "state", system_metrics_task_state_to_string(task->state));
        cJSON_AddNumberToObject(entry, "cpu_percent", task->cpu_percent);
        cJSON_AddNumberToObject(entry, "runtime_ticks", (double)task->runtime_ticks);
        cJSON_AddNumberToObject(entry, "stack_high_water_mark", (double)task->stack_high_water_mark);
        cJSON_AddNumberToObject(entry, "core", (double)task->core_id);

        cJSON_AddItemToArray(array, entry);
    }

    esp_err_t err = system_metrics_send_json(array, buffer, buffer_size, out_length);
    cJSON_Delete(array);
    return err;
}

esp_err_t system_metrics_modules_to_json(const system_metrics_module_snapshot_t *modules,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length)
{
    if (modules == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < modules->module_count; ++i) {
        const system_metrics_module_info_t *module = &modules->modules[i];
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(entry, "name", module->name);
        cJSON_AddStringToObject(entry, "status", system_metrics_module_status_to_string(module->status));
        cJSON_AddStringToObject(entry, "detail", module->detail);
        cJSON_AddStringToObject(entry, "last_event", module->last_event_iso);

        cJSON_AddItemToArray(array, entry);
    }

    esp_err_t err = system_metrics_send_json(array, buffer, buffer_size, out_length);
    cJSON_Delete(array);
    return err;
}

