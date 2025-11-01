#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double charged_wh;
    double discharged_wh;
} nvs_energy_state_t;

esp_err_t nvs_energy_init(void);
esp_err_t nvs_energy_load(nvs_energy_state_t *state);
esp_err_t nvs_energy_store(const nvs_energy_state_t *state);
esp_err_t nvs_energy_clear(void);

#ifdef __cplusplus
}
#endif

