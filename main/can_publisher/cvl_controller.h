#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "uart_bms.h"
#include "cvl_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t timestamp_ms;
    cvl_computation_result_t result;
} can_publisher_cvl_result_t;

void can_publisher_cvl_init(void);
void can_publisher_cvl_prepare(const uart_bms_live_data_t *data);
bool can_publisher_cvl_get_latest(can_publisher_cvl_result_t *out_result);

#ifdef __cplusplus
}
#endif

