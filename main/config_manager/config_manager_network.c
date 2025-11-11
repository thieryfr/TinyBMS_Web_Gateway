/**
 * @file config_manager_network.c
 * @brief Network and device configuration management
 *
 * This module manages device settings, WiFi configuration (STA and AP modes),
 * CAN bus settings, UART pins, and AP secret generation/storage.
 */

#include "config_manager.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"

#include "app_config.h"
#include "uart_bms.h"
#include "can_config_defaults.h"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#endif

#define CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH 8U
#define CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH    16U
#define CONFIG_MANAGER_WIFI_AP_SECRET_KEY    "wifi_ap_secret"
#define CONFIG_MANAGER_NAMESPACE "gateway_cfg"

#ifndef CONFIG_TINYBMS_WIFI_STA_SSID
#define CONFIG_TINYBMS_WIFI_STA_SSID ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_PASSWORD
#define CONFIG_TINYBMS_WIFI_STA_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_HOSTNAME
#define CONFIG_TINYBMS_WIFI_STA_HOSTNAME ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_MAX_RETRY
#define CONFIG_TINYBMS_WIFI_STA_MAX_RETRY 5
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_SSID
#define CONFIG_TINYBMS_WIFI_AP_SSID "TinyBMS-Gateway"
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_PASSWORD
#define CONFIG_TINYBMS_WIFI_AP_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_CHANNEL
#define CONFIG_TINYBMS_WIFI_AP_CHANNEL 1
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS
#define CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS 4
#endif

#ifndef CONFIG_TINYBMS_UART_TX_GPIO
#define CONFIG_TINYBMS_UART_TX_GPIO 37
#endif

#ifndef CONFIG_TINYBMS_UART_RX_GPIO
#define CONFIG_TINYBMS_UART_RX_GPIO 36
#endif

#ifndef CONFIG_TINYBMS_CAN_SERIAL_NUMBER
#define CONFIG_TINYBMS_CAN_SERIAL_NUMBER "TinyBMS-00000000"
#endif

static const char *TAG = "config_manager_network";

// Forward declarations for helper functions (shared with other modules)
extern void config_manager_copy_string(char *dest, size_t dest_size, const char *src);
extern esp_err_t config_manager_init_nvs(void);

// Network-related static variables
static config_manager_device_settings_t s_device_settings_snapshot = {0};
static config_manager_uart_pins_t s_uart_pins_snapshot = {0};
static config_manager_wifi_settings_t s_wifi_settings_snapshot = {0};
static config_manager_can_settings_t s_can_settings_snapshot = {0};
static char s_device_name_snapshot[CONFIG_MANAGER_DEVICE_NAME_MAX_LENGTH] = {0};

static config_manager_device_settings_t s_device_settings = {
    .name = APP_DEVICE_NAME,
};

static config_manager_uart_pins_t s_uart_pins = {
    .tx_gpio = CONFIG_TINYBMS_UART_TX_GPIO,
    .rx_gpio = CONFIG_TINYBMS_UART_RX_GPIO,
};

static config_manager_wifi_settings_t s_wifi_settings = {
    .sta = {
        .ssid = CONFIG_TINYBMS_WIFI_STA_SSID,
        .password = CONFIG_TINYBMS_WIFI_STA_PASSWORD,
        .hostname = CONFIG_TINYBMS_WIFI_STA_HOSTNAME,
        .max_retry = CONFIG_TINYBMS_WIFI_STA_MAX_RETRY,
    },
    .ap = {
        .ssid = CONFIG_TINYBMS_WIFI_AP_SSID,
        .password = CONFIG_TINYBMS_WIFI_AP_PASSWORD,
        .channel = CONFIG_TINYBMS_WIFI_AP_CHANNEL,
        .max_clients = CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS,
    },
};

static char s_wifi_ap_secret[CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH] = {0};
static bool s_wifi_ap_secret_loaded = false;

static config_manager_can_settings_t s_can_settings = {
    .twai = {
        .tx_gpio = CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO,
        .rx_gpio = CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO,
    },
    .keepalive = {
        .interval_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS,
        .timeout_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS,
        .retry_ms = CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS,
    },
    .publisher = {
        .period_ms = CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS,
    },
    .identity = {
        .handshake_ascii = CONFIG_TINYBMS_CAN_HANDSHAKE_ASCII,
        .manufacturer = CONFIG_TINYBMS_CAN_MANUFACTURER,
        .battery_name = CONFIG_TINYBMS_CAN_BATTERY_NAME,
        .battery_family = CONFIG_TINYBMS_CAN_BATTERY_FAMILY,
        .serial_number = CONFIG_TINYBMS_CAN_SERIAL_NUMBER,
    },
};

static void config_manager_generate_random_bytes(uint8_t *buffer, size_t length)
{
    if (buffer == NULL || length == 0) {
        return;
    }

#ifdef ESP_PLATFORM
    esp_fill_random(buffer, length);
#else
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        unsigned int seed = (unsigned int)time(NULL);
        seed ^= (unsigned int)clock();
        srand(seed);
    }
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

static void config_manager_generate_ap_secret(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const size_t alphabet_len = sizeof(alphabet) - 1U;
    if (alphabet_len == 0U) {
        out[0] = '\0';
        return;
    }

    uint8_t random_bytes[CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH];
    memset(random_bytes, 0, sizeof(random_bytes));
    config_manager_generate_random_bytes(random_bytes, sizeof(random_bytes));

    size_t required = CONFIG_MANAGER_WIFI_AP_SECRET_LENGTH + 1U;
    if (out_size < required) {
        required = out_size;
    }

    size_t limit = required - 1U;
    for (size_t i = 0; i < limit; ++i) {
        out[i] = alphabet[random_bytes[i] % alphabet_len];
    }
    out[limit] = '\0';
}

static void config_manager_store_ap_secret_to_nvs(const char *secret)
{
#ifdef ESP_PLATFORM
    if (secret == NULL || secret[0] == '\0') {
        return;
    }

    if (config_manager_init_nvs() != ESP_OK) {
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for AP secret: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(handle, CONFIG_MANAGER_WIFI_AP_SECRET_KEY, secret);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist AP secret: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
#else
    (void)secret;
#endif
}

static void config_manager_ensure_ap_secret_loaded(void)
{
    if (s_wifi_ap_secret_loaded) {
        return;
    }

#ifdef ESP_PLATFORM
    if (config_manager_init_nvs() != ESP_OK) {
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        s_wifi_ap_secret_loaded = true;
        return;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for Wi-Fi secret: %s", esp_err_to_name(err));
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        s_wifi_ap_secret_loaded = true;
        return;
    }

    size_t length = sizeof(s_wifi_ap_secret);
    err = nvs_get_str(handle, CONFIG_MANAGER_WIFI_AP_SECRET_KEY, s_wifi_ap_secret, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND ||
        strlen(s_wifi_ap_secret) < CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
        if (strlen(s_wifi_ap_secret) >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
            config_manager_store_ap_secret_to_nvs(s_wifi_ap_secret);
        }
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read AP secret from NVS: %s", esp_err_to_name(err));
        config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
    }

    nvs_close(handle);
#else
    config_manager_generate_ap_secret(s_wifi_ap_secret, sizeof(s_wifi_ap_secret));
#endif

    s_wifi_ap_secret_loaded = true;
}

static void config_manager_apply_ap_secret_if_needed(config_manager_wifi_settings_t *wifi)
{
    if (wifi == NULL) {
        return;
    }

    size_t password_len = strnlen(wifi->ap.password, sizeof(wifi->ap.password));
    if (password_len >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        return;
    }

    config_manager_ensure_ap_secret_loaded();
    if (strlen(s_wifi_ap_secret) >= CONFIG_MANAGER_WIFI_PASSWORD_MIN_LENGTH) {
        config_manager_copy_string(wifi->ap.password,
                                   sizeof(wifi->ap.password),
                                   s_wifi_ap_secret);
    } else {
        ESP_LOGW(TAG, "No valid AP secret available; fallback AP will remain disabled");
    }
}

// Forward declarations for functions from other modules
extern esp_err_t config_manager_lock(TickType_t timeout);
extern void config_manager_unlock(void);
extern void config_manager_ensure_initialised(void);
extern esp_err_t config_manager_store_poll_interval(uint32_t interval_ms);
extern esp_err_t config_manager_build_config_snapshot_locked(void);
extern void config_manager_publish_config_snapshot(void);
extern esp_err_t config_manager_save_config_file(void);
extern uint32_t config_manager_clamp_poll_interval(uint32_t interval_ms);

// Access to core static variables
extern uint32_t s_uart_poll_interval_ms;
extern bool s_config_file_loaded;

const char *config_manager_effective_device_name_impl(void)
{
    if (s_device_settings.name[0] != '\0') {
        return s_device_settings.name;
    }
    return APP_DEVICE_NAME;
}

uint32_t config_manager_get_uart_poll_interval_ms(void)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning default UART interval due to lock failure");
        return UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    }

    uint32_t interval = s_uart_poll_interval_ms;
    config_manager_unlock();
    return interval;
}

esp_err_t config_manager_set_uart_poll_interval_ms(uint32_t interval_ms)
{
    config_manager_ensure_initialised();

    esp_err_t lock_err = config_manager_lock(CONFIG_MANAGER_MUTEX_TIMEOUT_TICKS);
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    uint32_t clamped = config_manager_clamp_poll_interval(interval_ms);
    if (clamped == s_uart_poll_interval_ms) {
        config_manager_unlock();
        uart_bms_set_poll_interval_ms(clamped);
        return ESP_OK;
    }

    s_uart_poll_interval_ms = clamped;
    uart_bms_set_poll_interval_ms(clamped);

    esp_err_t persist_err = config_manager_store_poll_interval(clamped);
    if (persist_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist UART poll interval: %s", esp_err_to_name(persist_err));
    }

    esp_err_t snapshot_err = config_manager_build_config_snapshot_locked();
    if (snapshot_err == ESP_OK) {
        config_manager_publish_config_snapshot();
        if (persist_err == ESP_OK && s_config_file_loaded) {
            esp_err_t save_err = config_manager_save_config_file();
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to update configuration file: %s", esp_err_to_name(save_err));
            }
        }
    }

    config_manager_unlock();

    if (persist_err != ESP_OK) {
        return persist_err;
    }
    return snapshot_err;
}

const config_manager_uart_pins_t *config_manager_get_uart_pins(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning UART pins without lock");
        return &s_uart_pins;
    }

    s_uart_pins_snapshot = s_uart_pins;
    config_manager_unlock();
    return &s_uart_pins_snapshot;
}

const config_manager_device_settings_t *config_manager_get_device_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning device settings without lock");
        return &s_device_settings;
    }

    s_device_settings_snapshot = s_device_settings;
    config_manager_unlock();
    return &s_device_settings_snapshot;
}

const char *config_manager_get_device_name(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning device name without lock");
        return config_manager_effective_device_name_impl();
    }

    const char *effective = config_manager_effective_device_name_impl();
    config_manager_copy_string(s_device_name_snapshot,
                               sizeof(s_device_name_snapshot),
                               effective);
    config_manager_unlock();
    return s_device_name_snapshot;
}

const config_manager_wifi_settings_t *config_manager_get_wifi_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning WiFi settings without lock");
        return &s_wifi_settings;
    }

    s_wifi_settings_snapshot = s_wifi_settings;
    config_manager_unlock();
    return &s_wifi_settings_snapshot;
}

const config_manager_can_settings_t *config_manager_get_can_settings(void)
{
    config_manager_ensure_initialised();
    esp_err_t lock_err = config_manager_lock(portMAX_DELAY);
    if (lock_err != ESP_OK) {
        ESP_LOGW(TAG, "Returning CAN settings without lock");
        return &s_can_settings;
    }

    s_can_settings_snapshot = s_can_settings;
    config_manager_unlock();
    return &s_can_settings_snapshot;
}
