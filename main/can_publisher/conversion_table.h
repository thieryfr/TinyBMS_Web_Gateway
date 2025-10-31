#pragma once

#include <stddef.h>

#include "can_publisher.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const can_publisher_channel_t g_can_publisher_channels[];
extern const size_t g_can_publisher_channel_count;

void can_publisher_conversion_reset_state(void);

#ifdef __cplusplus
}
#endif

