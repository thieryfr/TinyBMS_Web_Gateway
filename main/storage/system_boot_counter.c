#include "system_boot_counter.h"

#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SYSTEM_BOOT_COUNTER_NAMESPACE "tinybms_sys"
#define SYSTEM_BOOT_COUNTER_KEY       "boot_count"

static const char *TAG = "boot_counter";

static bool s_nvs_ready = false;
static bool s_boot_loaded = false;
static uint32_t s_boot_count = 0;

static esp_err_t system_boot_counter_ensure_nvs(void)
{
    if (s_nvs_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed (%s), erasing partition", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            return erase_err;
        }
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_nvs_ready = true;
    }
    return err;
}

static esp_err_t system_boot_counter_load(void)
{
    if (s_boot_loaded) {
        return ESP_OK;
    }

    esp_err_t err = system_boot_counter_ensure_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(SYSTEM_BOOT_COUNTER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t stored = 0;
    err = nvs_get_u32(handle, SYSTEM_BOOT_COUNTER_KEY, &stored);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        stored = 0;
        err = ESP_OK;
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        s_boot_count = stored;
        s_boot_loaded = true;
    }

    return err;
}

esp_err_t system_boot_counter_init(void)
{
    esp_err_t err = system_boot_counter_load();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to load boot counter: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t system_boot_counter_increment_and_get(uint32_t *out_value)
{
    esp_err_t err = system_boot_counter_load();
    if (err != ESP_OK) {
        return err;
    }

    uint32_t next = s_boot_count + 1U;

    nvs_handle_t handle = 0;
    err = nvs_open(SYSTEM_BOOT_COUNTER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, SYSTEM_BOOT_COUNTER_KEY, next);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        s_boot_count = next;
        s_boot_loaded = true;
        if (out_value != NULL) {
            *out_value = s_boot_count;
        }
    } else {
        ESP_LOGW(TAG, "Failed to persist boot counter: %s", esp_err_to_name(err));
    }

    return err;
}

uint32_t system_boot_counter_get(void)
{
    if (!s_boot_loaded) {
        esp_err_t err = system_boot_counter_load();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Boot counter unavailable: %s", esp_err_to_name(err));
            return 0U;
        }
    }
    return s_boot_count;
}

#else  // !ESP_PLATFORM

static uint32_t s_boot_count = 0;
static bool s_boot_loaded = false;

esp_err_t system_boot_counter_init(void)
{
    s_boot_loaded = true;
    return ESP_OK;
}

esp_err_t system_boot_counter_increment_and_get(uint32_t *out_value)
{
    if (!s_boot_loaded) {
        system_boot_counter_init();
    }
    s_boot_count += 1U;
    if (out_value != NULL) {
        *out_value = s_boot_count;
    }
    return ESP_OK;
}

uint32_t system_boot_counter_get(void)
{
    if (!s_boot_loaded) {
        system_boot_counter_init();
    }
    return s_boot_count;
}

void system_boot_counter_mock_reset(void)
{
    s_boot_count = 0U;
    s_boot_loaded = false;
}

void system_boot_counter_mock_set(uint32_t value)
{
    s_boot_count = value;
    s_boot_loaded = true;
}

#endif  // ESP_PLATFORM

