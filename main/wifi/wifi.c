#include "wifi.h"

#include "wifi_events.h"
#include "wifi_state.h"
#include "wifi_state_machine.h"

#include "esp_log.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#endif

static const char *TAG = "wifi";
static wifi_shared_state_t s_wifi_state = {0};

void wifi_set_event_publisher(event_bus_publish_fn_t publisher)
{
    wifi_events_set_publisher(&s_wifi_state, publisher);
}

void wifi_start_sta_mode(void)
{
    wifi_state_machine_start_sta(&s_wifi_state);
}

void wifi_init(void)
{
#if CONFIG_TINYBMS_WIFI_ENABLE
#ifdef ESP_PLATFORM
    if (s_wifi_state.initialised) {
        return;
    }

    wifi_state_machine_init(&s_wifi_state);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise NVS for Wi-Fi: %s", esp_err_to_name(err));
        return;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return;
    }

    if (s_wifi_state.sta_netif == NULL) {
        s_wifi_state.sta_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_state.sta_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create Wi-Fi STA network interface");
            return;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    err = wifi_state_machine_register_event_handlers(&s_wifi_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Wi-Fi event handlers: %s", esp_err_to_name(err));
        return;
    }

    err = wifi_state_machine_configure_sta(&s_wifi_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Wi-Fi station: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    if (!wifi_state_machine_sta_has_credentials()) {
#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
        ESP_LOGW(TAG, "Wi-Fi station SSID not configured, enabling fallback AP");
        wifi_state_machine_start_fallback_ap(&s_wifi_state);
#else
        ESP_LOGW(TAG, "Wi-Fi station SSID not configured and AP fallback disabled");
#endif
    }

    s_wifi_state.initialised = true;
    ESP_LOGI(TAG, "Wi-Fi initialised");
#else
    ESP_LOGI(TAG, "Wi-Fi module initialised (host build stub)");
#endif
#else
    ESP_LOGI(TAG, "Wi-Fi support disabled in configuration");
#endif
}

void wifi_deinit(void)
{
#if !CONFIG_TINYBMS_WIFI_ENABLE
    ESP_LOGI(TAG, "Wi-Fi support disabled, nothing to deinitialize");
    return;
#endif

#ifdef ESP_PLATFORM
    if (!s_wifi_state.initialised) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing WiFi...");

    wifi_state_machine_unregister_event_handlers(&s_wifi_state);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
    }

    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinit WiFi: %s", esp_err_to_name(err));
    }

    if (s_wifi_state.sta_netif != NULL) {
        esp_netif_destroy(s_wifi_state.sta_netif);
        s_wifi_state.sta_netif = NULL;
    }
    if (s_wifi_state.ap_netif != NULL) {
        esp_netif_destroy(s_wifi_state.ap_netif);
        s_wifi_state.ap_netif = NULL;
    }

    wifi_state_machine_deinit(&s_wifi_state);
    wifi_state_clear_publisher(&s_wifi_state);

    ESP_LOGI(TAG, "WiFi deinitialized");
#else
    ESP_LOGI(TAG, "WiFi module deinitialized (host build stub)");
#endif
}
