#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#include "event_bus.h"
#include "uart_bms.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    size_t size_bytes;
    time_t modified_time;
} history_logger_file_info_t;

typedef struct {
    uint64_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    float state_of_charge_pct;
    float state_of_health_pct;
    float average_temperature_c;
    char timestamp_iso[32];
} history_logger_archive_sample_t;

typedef struct {
    size_t total_samples;
    size_t returned_samples;
    size_t start_index;
    size_t buffer_capacity;
    history_logger_archive_sample_t *samples;
} history_logger_archive_t;

void history_logger_init(void);
void history_logger_deinit(void);
void history_logger_set_event_publisher(event_bus_publish_fn_t publisher);
void history_logger_handle_sample(const uart_bms_live_data_t *sample);

esp_err_t history_logger_list_files(history_logger_file_info_t **out_files,
                                    size_t *out_count,
                                    bool *out_mounted);
void history_logger_free_file_list(history_logger_file_info_t *files);

esp_err_t history_logger_load_archive(const char *filename, size_t limit, history_logger_archive_t *out_archive);
void history_logger_free_archive(history_logger_archive_t *archive);

esp_err_t history_logger_resolve_path(const char *filename, char *buffer, size_t buffer_size);
const char *history_logger_directory(void);

#ifdef __cplusplus
}
#endif

