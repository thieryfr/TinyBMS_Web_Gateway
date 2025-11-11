/**
 * @file config_manager_core.c
 * @brief Core configuration manager functions
 *
 * This module contains the core initialization, NVS operations, mutex handling,
 * event publishing, and configuration snapshot management. It serves as the
 * central coordination point for all config_manager modules.
 */

#include "config_manager.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_events.h"
#include "app_config.h"
#include "uart_bms.h"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#endif

#define CONFIG_MANAGER_REGISTER_EVENT_BUFFERS 4
#define CONFIG_MANAGER_MAX_UPDATE_PAYLOAD     192
#define CONFIG_MANAGER_MAX_REGISTER_KEY       32
#define CONFIG_MANAGER_NAMESPACE              "gateway_cfg"
#define CONFIG_MANAGER_POLL_KEY               "uart_poll"
#define CONFIG_MANAGER_REGISTER_KEY_PREFIX    "reg"
#define CONFIG_MANAGER_REGISTER_KEY_MAX       16

#define CONFIG_MANAGER_FS_BASE_PATH "/spiffs"
#define CONFIG_MANAGER_CONFIG_FILE  CONFIG_MANAGER_FS_BASE_PATH "/config.json"

static const char *TAG = "config_manager";

// Include register descriptors
#include "generated_tiny_rw_registers.inc"

// Forward declarations for functions from other modules
extern void config_manager_copy_string(char *dest, size_t dest_size, const char *src);
extern const char *config_manager_effective_device_name_impl(void);
extern void config_manager_ensure_topics_loaded(void);
extern void config_manager_load_mqtt_settings_from_nvs(nvs_handle_t handle);
extern esp_err_t config_manager_render_config_snapshot_locked(bool include_secrets,
                                                               char *buffer,
                                                               size_t buffer_size,
                                                               size_t *out_length);
extern esp_err_t config_manager_apply_config_payload(const char *json,
                                                      size_t length,
                                                      bool persist,
                                                      bool apply_runtime);
extern uint32_t config_manager_clamp_poll_interval(uint32_t interval_ms);
extern float config_manager_raw_to_user(const config_manager_register_descriptor_t *desc, uint16_t raw_value);
extern esp_err_t config_manager_align_raw_value(const config_manager_register_descriptor_t *desc,
                                                 float requested_raw,
                                                 uint16_t *out_raw);
extern void config_manager_apply_ap_secret_if_needed(config_manager_wifi_settings_t *wifi);

// External static variables (shared with other modules)
extern config_manager_device_settings_t s_device_settings;
extern config_manager_uart_pins_t s_uart_pins;
extern config_manager_wifi_settings_t s_wifi_settings;
extern config_manager_can_settings_t s_can_settings;
extern mqtt_client_config_t s_mqtt_config;
extern config_manager_mqtt_topics_t s_mqtt_topics;

// Core static variables
event_bus_publish_fn_t s_event_publisher = NULL;
char s_config_json_full[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
size_t s_config_length_full = 0;
char s_config_json_public[CONFIG_MANAGER_MAX_CONFIG_SIZE] = {0};
size_t s_config_length_public = 0;
uint16_t s_register_raw_values[s_register_count];
static bool s_registers_initialised = false;
static char s_register_events[CONFIG_MANAGER_REGISTER_EVENT_BUFFERS][CONFIG_MANAGER_MAX_UPDATE_PAYLOAD];
static size_t s_next_register_event = 0;
uint32_t s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
static bool s_settings_loaded = false;
bool s_config_file_loaded = false;
#ifdef ESP_PLATFORM
static bool s_spiffs_mounted = false;
static bool s_nvs_initialised = false;
#endif

// Mutex to protect access to global configuration state
// NOTE: Currently protects write operations (setters) only.
// TODO: Full thread safety requires protecting all config structure access
static SemaphoreHandle_t s_config_mutex = NULL;
static const TickType_t CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);

static void config_manager_make_register_key(uint16_t address, char *out_key, size_t out_size)
{
    if (out_key == NULL || out_size == 0) {
        return;
    }
    if (snprintf(out_key, out_size, CONFIG_MANAGER_REGISTER_KEY_PREFIX "%04X", (unsigned)address) >= (int)out_size) {
        out_key[out_size - 1] = '\0';
    }
}

esp_err_t config_manager_lock(TickType_t timeout)
{
    if (s_config_mutex == NULL) {
        ESP_LOGE(TAG, "Config mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_config_mutex, timeout) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire config mutex");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void config_manager_unlock(void)
{
    if (s_config_mutex != NULL) {
        xSemaphoreGive(s_config_mutex);
    }
}

#ifdef ESP_PLATFORM
esp_err_t config_manager_init_nvs(void)
{
    if (s_nvs_initialised) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS partition due to %s", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            return erase_err;
        }
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_nvs_initialised = true;
    } else {
        ESP_LOGW(TAG, "Failed to initialise NVS: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

static void config_manager_load_persistent_settings(void)
{
    if (s_settings_loaded) {
        return;
    }

    s_settings_loaded = true;
#ifdef ESP_PLATFORM
    if (config_manager_init_nvs() != ESP_OK) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    uint32_t stored_interval = 0;
    err = nvs_get_u32(handle, CONFIG_MANAGER_POLL_KEY, &stored_interval);
    if (err == ESP_OK) {
        s_uart_poll_interval_ms = config_manager_clamp_poll_interval(stored_interval);
    }

    config_manager_load_mqtt_settings_from_nvs(handle);
    nvs_close(handle);
#else
    s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    extern void config_manager_load_mqtt_settings_from_nvs(void);
    config_manager_load_mqtt_settings_from_nvs();
#endif

    extern esp_err_t config_manager_load_config_file(bool apply_runtime);
    esp_err_t file_err = config_manager_load_config_file(false);
    if (file_err != ESP_OK && file_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load configuration file: %s", esp_err_to_name(file_err));
    }

    for (size_t i = 0; i < s_register_count; ++i) {
        const config_manager_register_descriptor_t *desc = &s_register_descriptors[i];
        uint16_t stored_raw = 0;
        extern bool config_manager_load_register_raw(uint16_t address, uint16_t *out_value);
        if (!config_manager_load_register_raw(desc->address, &stored_raw)) {
            continue;
        }

        if (desc->value_class == CONFIG_MANAGER_VALUE_ENUM) {
            bool found = false;
            for (size_t e = 0; e < desc->enum_count; ++e) {
                if (desc->enum_values[e].value == stored_raw) {
                    found = true;
                    break;
                }
            }
            if (found) {
                s_register_raw_values[i] = stored_raw;
            }
            continue;
        }

        uint16_t aligned = 0;
        if (config_manager_align_raw_value(desc, (float)stored_raw, &aligned) == ESP_OK) {
            s_register_raw_values[i] = aligned;
        }
    }

    config_manager_apply_ap_secret_if_needed(&s_wifi_settings);
}

esp_err_t config_manager_store_poll_interval(uint32_t interval_ms)
{
#ifdef ESP_PLATFORM
    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, CONFIG_MANAGER_POLL_KEY, interval_ms);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
#else
    (void)interval_ms;
    return ESP_OK;
#endif
}

#ifdef ESP_PLATFORM
static esp_err_t config_manager_mount_spiffs(void)
{
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_MANAGER_FS_BASE_PATH,
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        s_spiffs_mounted = true;
        return ESP_OK;
    }

    if (err == ESP_OK) {
        s_spiffs_mounted = true;
    }
    return err;
}
#endif

esp_err_t config_manager_save_config_file(void)
{
#ifdef ESP_PLATFORM
    esp_err_t mount_err = config_manager_mount_spiffs();
    if (mount_err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to mount SPIFFS for config save: %s", esp_err_to_name(mount_err));
        return mount_err;
    }
#endif

    FILE *file = fopen(CONFIG_MANAGER_CONFIG_FILE, "w");
    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to open %s for writing: errno=%d", CONFIG_MANAGER_CONFIG_FILE, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(s_config_json_full, 1, s_config_length_full, file);
    int flush_result = fflush(file);
    int close_result = fclose(file);
    if (written != s_config_length_full || flush_result != 0 || close_result != 0) {
        ESP_LOGW(TAG,
                 "Failed to write configuration file (written=%zu expected=%zu errno=%d)",
                 written,
                 s_config_length_full,
                 errno);
        return ESP_FAIL;
    }

    s_config_file_loaded = true;
    return ESP_OK;
}

esp_err_t config_manager_load_config_file(bool apply_runtime)
{
#ifdef ESP_PLATFORM
    esp_err_t mount_err = config_manager_mount_spiffs();
    if (mount_err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to mount SPIFFS for config load: %s", esp_err_to_name(mount_err));
        return mount_err;
    }
#endif

    FILE *file = fopen(CONFIG_MANAGER_CONFIG_FILE, "r");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1U, file);
    fclose(file);
    if (read == 0U) {
        ESP_LOGW(TAG, "Configuration file %s is empty", CONFIG_MANAGER_CONFIG_FILE);
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[read] = '\0';
    esp_err_t err = config_manager_apply_config_payload(buffer, read, false, apply_runtime);
    if (err == ESP_OK) {
        s_config_file_loaded = true;
    }
    return err;
}

#ifdef ESP_PLATFORM
esp_err_t config_manager_store_register_raw(uint16_t address, uint16_t raw_value)
{
    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[CONFIG_MANAGER_REGISTER_KEY_MAX];
    config_manager_make_register_key(address, key, sizeof(key));

    err = nvs_set_u16(handle, key, raw_value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool config_manager_load_register_raw(uint16_t address, uint16_t *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    esp_err_t err = config_manager_init_nvs();
    if (err != ESP_OK) {
        return false;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    char key[CONFIG_MANAGER_REGISTER_KEY_MAX];
    config_manager_make_register_key(address, key, sizeof(key));
    uint16_t value = 0;
    err = nvs_get_u16(handle, key, &value);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    *out_value = value;
    return true;
}
#else
esp_err_t config_manager_store_register_raw(uint16_t address, uint16_t raw_value)
{
    (void)address;
    (void)raw_value;
    return ESP_OK;
}

bool config_manager_load_register_raw(uint16_t address, uint16_t *out_value)
{
    (void)address;
    (void)out_value;
    return false;
}
#endif

void config_manager_publish_config_snapshot(void)
{
    if (s_event_publisher == NULL || s_config_length_public == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CONFIG_UPDATED,
        .payload = s_config_json_public,
        .payload_size = s_config_length_public + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish configuration snapshot");
    }
}

void config_manager_publish_register_change(const config_manager_register_descriptor_t *desc,
                                             uint16_t raw_value)
{
    if (s_event_publisher == NULL || desc == NULL) {
        return;
    }

    size_t slot = s_next_register_event;
    s_next_register_event = (s_next_register_event + 1) % CONFIG_MANAGER_REGISTER_EVENT_BUFFERS;

    char *payload = s_register_events[slot];
    float user_value = (desc->value_class == CONFIG_MANAGER_VALUE_ENUM)
                           ? (float)raw_value
                           : config_manager_raw_to_user(desc, raw_value);
    int precision = (desc->value_class == CONFIG_MANAGER_VALUE_ENUM) ? 0 : desc->precision;
    int written = snprintf(payload,
                           CONFIG_MANAGER_MAX_UPDATE_PAYLOAD,
                           "{\"type\":\"register_update\",\"key\":\"%s\",\"value\":%.*f,\"raw\":%u}",
                           desc->key,
                           precision,
                           user_value,
                           (unsigned)raw_value);
    if (written < 0 || written >= CONFIG_MANAGER_MAX_UPDATE_PAYLOAD) {
        ESP_LOGW(TAG, "Register update payload truncated for %s", desc->key);
        return;
    }

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CONFIG_UPDATED,
        .payload = payload,
        .payload_size = (size_t)written + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish register update for %s", desc->key);
    }
}

esp_err_t config_manager_build_config_snapshot_locked(void)
{
    config_manager_ensure_topics_loaded();

    esp_err_t err = config_manager_render_config_snapshot_locked(true,
                                                                 s_config_json_full,
                                                                 sizeof(s_config_json_full),
                                                                 &s_config_length_full);
    if (err != ESP_OK) {
        return err;
    }

    err = config_manager_render_config_snapshot_locked(false,
                                                       s_config_json_public,
                                                       sizeof(s_config_json_public),
                                                       &s_config_length_public);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t config_manager_build_config_snapshot(void)
{
    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    esp_err_t result = config_manager_build_config_snapshot_locked();
    config_manager_unlock();
    return result;
}

static void config_manager_load_register_defaults(void)
{
    for (size_t i = 0; i < s_register_count; ++i) {
        s_register_raw_values[i] = s_register_descriptors[i].default_raw;
    }
    s_registers_initialised = true;
}

void config_manager_ensure_initialised(void)
{
    // Initialize mutex on first call (thread-safe in FreeRTOS)
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (s_config_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create config mutex");
        }
    }

    if (!s_registers_initialised) {
        config_manager_load_register_defaults();
    }

    if (!s_settings_loaded) {
        config_manager_load_persistent_settings();
    }

    if (s_config_length_public == 0) {
        if (config_manager_build_config_snapshot() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to build default configuration snapshot");
        }
    }
}

void config_manager_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void config_manager_init(void)
{
    config_manager_ensure_initialised();
    uart_bms_set_poll_interval_ms(s_uart_poll_interval_ms);
}

const char *config_manager_mask_secret(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "";
    }
    return CONFIG_MANAGER_SECRET_MASK;
}

void config_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing config manager...");

#ifdef ESP_PLATFORM
    // Unmount SPIFFS if mounted
    if (s_spiffs_mounted) {
        esp_err_t err = esp_vfs_spiffs_unregister(NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unmount SPIFFS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "SPIFFS unmounted");
        }
        s_spiffs_mounted = false;
    }
#endif

    // Destroy mutex
    if (s_config_mutex != NULL) {
        vSemaphoreDelete(s_config_mutex);
        s_config_mutex = NULL;
    }

    // Reset state
    s_event_publisher = NULL;
    s_config_length_full = 0;
    s_config_length_public = 0;
    s_registers_initialised = false;
    s_settings_loaded = false;
#ifdef ESP_PLATFORM
    s_nvs_initialised = false;
#endif
    extern bool s_mqtt_topics_loaded;
    s_mqtt_topics_loaded = false;
    s_config_file_loaded = false;
    s_next_register_event = 0;
    s_uart_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    memset(s_config_json_full, 0, sizeof(s_config_json_full));
    memset(s_config_json_public, 0, sizeof(s_config_json_public));
    memset(s_register_raw_values, 0, sizeof(s_register_raw_values));
    memset(s_register_events, 0, sizeof(s_register_events));
    memset(&s_mqtt_config, 0, sizeof(s_mqtt_config));
    memset(&s_mqtt_topics, 0, sizeof(s_mqtt_topics));
    memset(&s_device_settings, 0, sizeof(s_device_settings));
    memset(&s_uart_pins, 0, sizeof(s_uart_pins));
    memset(&s_wifi_settings, 0, sizeof(s_wifi_settings));
    memset(&s_can_settings, 0, sizeof(s_can_settings));
    extern char s_wifi_ap_secret[];
    extern bool s_wifi_ap_secret_loaded;
    memset(s_wifi_ap_secret, 0, CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH);
    s_wifi_ap_secret_loaded = false;

    ESP_LOGI(TAG, "Config manager deinitialized");
}
