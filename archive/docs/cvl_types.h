#pragma once

#include <stdint.h>

enum CVLState : uint8_t {
    CVL_BULK = 0,
    CVL_TRANSITION = 1,
    CVL_FLOAT_APPROACH = 2,
    CVL_FLOAT = 3,
    CVL_IMBALANCE_HOLD = 4,
    CVL_SUSTAIN = 5
};

