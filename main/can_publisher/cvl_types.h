#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CVL_STATE_BULK = 0,
    CVL_STATE_TRANSITION = 1,
    CVL_STATE_FLOAT_APPROACH = 2,
    CVL_STATE_FLOAT = 3,
    CVL_STATE_IMBALANCE_HOLD = 4,
    CVL_STATE_SUSTAIN = 5,
} cvl_state_t;

#ifdef __cplusplus
}
#endif

