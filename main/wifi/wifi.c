#include "wifi.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "config_manager.h"
#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#endif

#include "app_events.h"

#ifndef CONFIG_TINYBMS_WIFI_STA_MAX_RETRY
#define CONFIG_TINYBMS_WIFI_STA_MAX_RETRY 5
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS
#define CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS 4
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_CHANNEL
#define CONFIG_TINYBMS_WIFI_AP_CHANNEL 1
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_SSID
#define CONFIG_TINYBMS_WIFI_STA_SSID ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_PASSWORD
#define CONFIG_TINYBMS_WIFI_STA_PASSWORD ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_STA_HOSTNAME
#define CONFIG_TINYBMS_WIFI_STA_HOSTNAME ""
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_SSID
#define CONFIG_TINYBMS_WIFI_AP_SSID "TinyBMS-Gateway"
#endif

#ifndef CONFIG_TINYBMS_WIFI_AP_PASSWORD
#define CONFIG_TINYBMS_WIFI_AP_PASSWORD ""
#endif

static const char *TAG = "wifi";

static const config_manager_wifi_settings_t *wifi_get_settings(void)
{
    static const config_manager_wifi_settings_t defaults = {
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

    const config_manager_wifi_settings_t *settings = config_manager_get_wifi_settings();
    return (settings != NULL) ? settings : &defaults;
}

static event_bus_publish_fn_t s_event_publisher = NULL;

#ifdef ESP_PLATFORM
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_wifi_initialised = false;
static bool s_ap_fallback_active = false;
static int s_retry_count = 0;
static esp_event_handler_instance_t s_wifi_event_handle = NULL;
static esp_event_handler_instance_t s_ip_got_handle = NULL;
static esp_event_handler_instance_t s_ip_lost_handle = NULL;

static void wifi_publish_event(app_event_id_t id)
{
    if (s_event_publisher == NULL) {
        return;
    }

    event_bus_event_t event = {
        .id = id,
        .payload = NULL,
        .payload_size = 0,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(25))) {
        ESP_LOGW(TAG, "Failed to publish Wi-Fi event 0x%08" PRIx32, (uint32_t)id);
    }
}

static void wifi_attempt_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
}

static void wifi_start_ap_mode(void)
{
#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
    if (s_ap_fallback_active) {
        return;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create Wi-Fi AP network interface");
            return;
        }
    }

    wifi_config_t ap_config = {0};
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    const char *ap_ssid = settings->ap.ssid;
    const char *ap_password = settings->ap.password;
    uint8_t ap_channel = settings->ap.channel > 0 ? settings->ap.channel : CONFIG_TINYBMS_WIFI_AP_CHANNEL;
    if (ap_channel > 13U) {
        ap_channel = 13U;
    }
    uint8_t ap_max_clients = settings->ap.max_clients > 0 ? settings->ap.max_clients : CONFIG_TINYBMS_WIFI_AP_MAX_CLIENTS;
    if (ap_max_clients > 10U) {
        ap_max_clients = 10U;
    }

    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen((const char *)ap_config.ap.ssid);
    ap_config.ap.channel = ap_channel;
    ap_config.ap.max_connection = ap_max_clients;
    ap_config.ap.beacon_interval = 100;

    size_t password_len = strlen(ap_password);
    if (password_len > 0 && password_len < 8) {
        ESP_LOGW(TAG, "Fallback AP password too short, switching to open network");
        password_len = 0;
    }

    if (password_len > 0) {
        strlcpy((char *)ap_config.ap.password,
                ap_password,
                sizeof(ap_config.ap.password));
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        ap_config.ap.pmf_cfg = (wifi_pmf_config_t){
            .capable = true,
            .required = false,
        };
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_LOGW(TAG, "Starting Wi-Fi fallback access point '%s'", ap_config.ap.ssid);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_fallback_active = true;
    s_retry_count = 0;
#else
    ESP_LOGW(TAG, "Wi-Fi connection failed and AP fallback disabled");
#endif
}

static void wifi_configure_sta(void)
{
    wifi_config_t sta_config = {0};
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    const char *sta_ssid = settings->sta.ssid;
    const char *sta_password = settings->sta.password;

    strlcpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, sta_password, sizeof(sta_config.sta.password));

    size_t password_len = strlen(sta_password);
    if (password_len > 0 && password_len < 8) {
        ESP_LOGW(TAG, "Wi-Fi password shorter than 8 characters, attempting connection anyway");
    }

    if (password_len == 0) {
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg = (wifi_pmf_config_t){
            .capable = true,
            .required = false,
        };
    }

    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    if (s_sta_netif != NULL && strlen(settings->sta.hostname) > 0) {
        esp_err_t err = esp_netif_set_hostname(s_sta_netif, settings->sta.hostname);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    const char *sta_ssid = settings->sta.ssid;
    uint8_t max_retry = settings->sta.max_retry > 0 ? settings->sta.max_retry : CONFIG_TINYBMS_WIFI_STA_MAX_RETRY;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                wifi_publish_event(APP_EVENT_ID_WIFI_STA_START);
                ESP_LOGI(TAG, "Wi-Fi station started, connecting to '%s'", sta_ssid);
                wifi_attempt_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Wi-Fi connected to '%s'", sta_ssid);
                s_retry_count = 0;
                wifi_publish_event(APP_EVENT_ID_WIFI_STA_CONNECTED);
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_publish_event(APP_EVENT_ID_WIFI_STA_DISCONNECTED);
                if (event_data != NULL) {
                    const wifi_event_sta_disconnected_t *info = (const wifi_event_sta_disconnected_t *)event_data;
                    ESP_LOGW(TAG, "Station disconnected, reason=%d", info->reason);
                } else {
                    ESP_LOGW(TAG, "Station disconnected");
                }

                if (s_ap_fallback_active) {
                    ESP_LOGW(TAG, "Station disconnected while fallback AP active");
                    break;
                }

                if (s_retry_count < max_retry) {
                    s_retry_count++;
                    ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_retry_count, max_retry);
                    wifi_attempt_connect();
                } else {
                    ESP_LOGE(TAG, "Wi-Fi failed to connect after %d attempts", max_retry);
#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
                    wifi_start_ap_mode();
#else
                    s_retry_count = 0;
                    ESP_LOGW(TAG, "Fallback AP disabled, continuing to retry station connection");
                    wifi_attempt_connect();
#endif
                }
                break;
            }
            case WIFI_EVENT_AP_START:
                wifi_publish_event(APP_EVENT_ID_WIFI_AP_STARTED);
                ESP_LOGI(TAG, "Wi-Fi access point started");
                break;
            case WIFI_EVENT_AP_STOP:
                wifi_publish_event(APP_EVENT_ID_WIFI_AP_STOPPED);
                ESP_LOGI(TAG, "Wi-Fi access point stopped");
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_publish_event(APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED);
                if (event_data != NULL) {
                    const wifi_event_ap_staconnected_t *info = (const wifi_event_ap_staconnected_t *)event_data;
                    ESP_LOGI(TAG,
                             "Client %02x:%02x:%02x:%02x:%02x:%02x joined AP, AID=%d",
                             info->mac[0],
                             info->mac[1],
                             info->mac[2],
                             info->mac[3],
                             info->mac[4],
                             info->mac[5],
                             info->aid);
                } else {
                    ESP_LOGI(TAG, "Client connected to access point");
                }
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_publish_event(APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED);
                if (event_data != NULL) {
                    const wifi_event_ap_stadisconnected_t *info = (const wifi_event_ap_stadisconnected_t *)event_data;
                    ESP_LOGI(TAG,
                             "Client %02x:%02x:%02x:%02x:%02x:%02x left AP, AID=%d",
                             info->mac[0],
                             info->mac[1],
                             info->mac[2],
                             info->mac[3],
                             info->mac[4],
                             info->mac[5],
                             info->aid);
                } else {
                    ESP_LOGI(TAG, "Client disconnected from access point");
                }
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            wifi_publish_event(APP_EVENT_ID_WIFI_STA_GOT_IP);
            s_retry_count = 0;
            s_ap_fallback_active = false;
            if (event_data != NULL) {
                const ip_event_got_ip_t *ip_event = (const ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG,
                         "Wi-Fi station obtained IP address: %s",
                         ip4addr_ntoa(&ip_event->ip_info.ip));
            } else {
                ESP_LOGI(TAG, "Wi-Fi station obtained IP address");
            }
        } else if (event_id == IP_EVENT_STA_LOST_IP) {
            wifi_publish_event(APP_EVENT_ID_WIFI_STA_LOST_IP);
            ESP_LOGW(TAG, "Wi-Fi station lost IP address");
        }
    }
}
#endif

void wifi_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void wifi_init(void)
{
#if CONFIG_TINYBMS_WIFI_ENABLE
#ifdef ESP_PLATFORM
    if (s_wifi_initialised) {
        return;
    }

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

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create Wi-Fi STA network interface");
            return;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &s_wifi_event_handle));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &s_ip_got_handle));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_LOST_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &s_ip_lost_handle));

    wifi_configure_sta();

    ESP_ERROR_CHECK(esp_wifi_start());

    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    if (strlen(settings->sta.ssid) == 0) {
#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
        ESP_LOGW(TAG, "Wi-Fi station SSID not configured, enabling fallback AP");
        wifi_start_ap_mode();
#else
        ESP_LOGW(TAG, "Wi-Fi station SSID not configured and AP fallback disabled");
#endif
    }

    s_wifi_initialised = true;
    ESP_LOGI(TAG, "Wi-Fi initialised");
#else
    ESP_LOGI(TAG, "Wi-Fi module initialised (host build stub)");
#endif
#else
    ESP_LOGI(TAG, "Wi-Fi support disabled in configuration");
#endif
}
