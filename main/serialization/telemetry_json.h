#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct uart_bms_live_data;
struct can_publisher_frame;

bool telemetry_json_write_metrics(const struct uart_bms_live_data *data,
                                  char *buffer,
                                  size_t buffer_size,
                                  size_t *out_length);

bool telemetry_json_write_can_ready(const struct can_publisher_frame *frame,
                                    char *buffer,
                                    size_t buffer_size,
                                    size_t *out_length);

bool telemetry_json_write_history_sample(const struct uart_bms_live_data *sample,
                                         time_t now,
                                         char *buffer,
                                         size_t buffer_size,
                                         size_t *out_length);

#ifdef __cplusplus
}
#endif

