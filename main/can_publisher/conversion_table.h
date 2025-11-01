#pragma once

#include <stddef.h>

#include "can_publisher.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const can_publisher_channel_t g_can_publisher_channels[];
extern const size_t g_can_publisher_channel_count;

void can_publisher_conversion_reset_state(void);
void can_publisher_conversion_set_energy_state(double charged_wh, double discharged_wh);
void can_publisher_conversion_get_energy_state(double *charged_wh, double *discharged_wh);
esp_err_t can_publisher_conversion_restore_energy_state(void);
esp_err_t can_publisher_conversion_persist_energy_state(void);

#ifdef __cplusplus
}
#endif

