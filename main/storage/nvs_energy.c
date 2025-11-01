#include "nvs_energy.h"

#include <math.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_ENERGY_NAMESPACE "energy"
#define NVS_ENERGY_KEY       "accum"

static const char *TAG = "nvs_energy";

static bool s_nvs_ready = false;

static double sanitize_energy_value(double value)
{
    if (!(value > 0.0) || !isfinite(value)) {
        return 0.0;
    }
    return value;
}

esp_err_t nvs_energy_init(void)
{
    if (s_nvs_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed (%s), erasing", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "Unable to erase NVS: %s", esp_err_to_name(erase_err));
            return erase_err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialise NVS: %s", esp_err_to_name(err));
        return err;
    }

    s_nvs_ready = true;
    return ESP_OK;
}

esp_err_t nvs_energy_load(nvs_energy_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_energy_init() != ESP_OK) {
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_ENERGY_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_energy_state_t stored = {0};
    size_t required = sizeof(stored);
    err = nvs_get_blob(handle, NVS_ENERGY_KEY, &stored, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (required != sizeof(stored)) {
        return ESP_ERR_INVALID_SIZE;
    }

    state->charged_wh = sanitize_energy_value(stored.charged_wh);
    state->discharged_wh = sanitize_energy_value(stored.discharged_wh);
    return ESP_OK;
}

esp_err_t nvs_energy_store(const nvs_energy_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_energy_init() != ESP_OK) {
        return ESP_FAIL;
    }

    nvs_energy_state_t sanitized = {
        .charged_wh = sanitize_energy_value(state->charged_wh),
        .discharged_wh = sanitize_energy_value(state->discharged_wh),
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_ENERGY_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_ENERGY_KEY, &sanitized, sizeof(sanitized));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_energy_clear(void)
{
    if (nvs_energy_init() != ESP_OK) {
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_ENERGY_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, NVS_ENERGY_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

#else  // !ESP_PLATFORM

static bool s_mock_initialised = false;
static bool s_mock_has_state = false;
static nvs_energy_state_t s_mock_state = {0};

esp_err_t nvs_energy_init(void)
{
    s_mock_initialised = true;
    return ESP_OK;
}

esp_err_t nvs_energy_load(nvs_energy_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mock_initialised) {
        nvs_energy_init();
    }
    if (!s_mock_has_state) {
        return ESP_ERR_NOT_FOUND;
    }
    *state = s_mock_state;
    return ESP_OK;
}

esp_err_t nvs_energy_store(const nvs_energy_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mock_initialised) {
        nvs_energy_init();
    }
    s_mock_state = *state;
    s_mock_has_state = true;
    return ESP_OK;
}

esp_err_t nvs_energy_clear(void)
{
    if (!s_mock_initialised) {
        nvs_energy_init();
    }
    memset(&s_mock_state, 0, sizeof(s_mock_state));
    s_mock_has_state = false;
    return ESP_OK;
}

#endif  // ESP_PLATFORM

