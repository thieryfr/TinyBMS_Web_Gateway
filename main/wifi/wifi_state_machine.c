#include "wifi_state_machine.h"

#include <string.h>

#include "esp_log.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#endif

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

#define WIFI_AP_MIN_PASSWORD_LENGTH 8U
#define WIFI_AP_STA_RETRY_INTERVAL_MS 60000U

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
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

bool wifi_state_machine_sta_has_credentials(void)
{
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    return settings != NULL && strlen(settings->sta.ssid) > 0U;
}

void wifi_state_machine_init(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

    event_bus_publish_fn_t publisher = state->publisher;
    wifi_state_reset(state);
    state->publisher = publisher;

#ifdef ESP_PLATFORM
    if (state->mutex == NULL) {
        state->mutex = xSemaphoreCreateMutex();
        if (state->mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create Wi-Fi state mutex");
        }
    }

    if (state->sta_retry_timer == NULL) {
        state->sta_retry_timer = xTimerCreate("wifi_sta_retry",
                                              pdMS_TO_TICKS(WIFI_AP_STA_RETRY_INTERVAL_MS),
                                              pdFALSE,
                                              state,
                                              wifi_state_machine_retry_timer_callback);
        if (state->sta_retry_timer == NULL) {
            ESP_LOGW(TAG, "Failed to allocate STA retry timer");
        }
    } else {
        vTimerSetTimerID(state->sta_retry_timer, state);
    }
#endif
}

void wifi_state_machine_deinit(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

#ifdef ESP_PLATFORM
    if (state->sta_retry_timer != NULL) {
        xTimerStop(state->sta_retry_timer, 0);
        xTimerDelete(state->sta_retry_timer, 0);
        state->sta_retry_timer = NULL;
    }

    if (state->mutex != NULL) {
        vSemaphoreDelete(state->mutex);
        state->mutex = NULL;
    }

    state->sta_netif = NULL;
    state->ap_netif = NULL;
    state->wifi_event_handle = NULL;
    state->ip_got_handle = NULL;
    state->ip_lost_handle = NULL;
#endif

    state->initialised = false;
    state->ap_fallback_active = false;
    state->retry_count = 0;
}

#ifdef ESP_PLATFORM
static bool wifi_state_lock(wifi_shared_state_t *state, TickType_t timeout)
{
    if (state == NULL || state->mutex == NULL) {
        return false;
    }

    return xSemaphoreTake(state->mutex, timeout) == pdTRUE;
}

static void wifi_state_unlock(wifi_shared_state_t *state)
{
    if (state == NULL || state->mutex == NULL) {
        return;
    }

    xSemaphoreGive(state->mutex);
}

static void wifi_stop_sta_retry_timer(wifi_shared_state_t *state)
{
    if (state == NULL || state->sta_retry_timer == NULL) {
        return;
    }

    if (xTimerIsTimerActive(state->sta_retry_timer) == pdTRUE) {
        if (xTimerStop(state->sta_retry_timer, 0) != pdPASS) {
            ESP_LOGW(TAG, "Failed to stop STA retry timer");
        }
    }
}

static void wifi_schedule_sta_retry(wifi_shared_state_t *state, uint32_t delay_ms)
{
    if (state == NULL || state->sta_retry_timer == NULL) {
        return;
    }

    TickType_t delay_ticks = pdMS_TO_TICKS(delay_ms);
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    if (xTimerIsTimerActive(state->sta_retry_timer) == pdTRUE) {
        if (xTimerStop(state->sta_retry_timer, 0) != pdPASS) {
            ESP_LOGW(TAG, "Failed to stop STA retry timer before reschedule");
            return;
        }
    }

    if (xTimerChangePeriod(state->sta_retry_timer, delay_ticks, 0) != pdPASS) {
        ESP_LOGW(TAG, "Failed to update STA retry timer period");
        return;
    }

    if (xTimerStart(state->sta_retry_timer, 0) != pdPASS) {
        ESP_LOGW(TAG, "Failed to start STA retry timer");
        return;
    }

    ESP_LOGI(TAG, "Scheduled STA retry in %u ms (one-shot)", delay_ms);
}

static void wifi_attempt_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
}

esp_err_t wifi_state_machine_configure_sta(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

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

    if (state->sta_netif != NULL && strlen(settings->sta.hostname) > 0) {
        esp_err_t err = esp_netif_set_hostname(state->sta_netif, settings->sta.hostname);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
        }
    }

    return ESP_OK;
}

static bool wifi_clear_ap_fallback_flag(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return false;
    }

    if (!wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        return false;
    }

    state->ap_fallback_active = false;
    wifi_state_unlock(state);
    return true;
}

esp_err_t wifi_state_machine_start_fallback_ap(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
    bool ap_flag_set = false;
    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        if (state->ap_fallback_active) {
            wifi_state_unlock(state);
            return ESP_OK;
        }
        state->ap_fallback_active = true;
        state->retry_count = 0;
        ap_flag_set = true;
        wifi_state_unlock(state);
    } else {
        ESP_LOGW(TAG, "Cannot start AP, mutex timeout");
        wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_FAILED);
        return ESP_FAIL;
    }

    if (state->ap_netif == NULL) {
        state->ap_netif = esp_netif_create_default_wifi_ap();
        if (state->ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create Wi-Fi AP network interface");
            if (ap_flag_set) {
                bool cleared = wifi_clear_ap_fallback_flag(state);
                if (!cleared) {
                    ESP_LOGW(TAG, "Failed to clear AP fallback flag after netif creation failure");
                }
            }
            wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_FAILED);
            return ESP_FAIL;
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
    if (password_len < WIFI_AP_MIN_PASSWORD_LENGTH) {
        ESP_LOGE(TAG,
                 "Fallback AP password shorter than %u characters, refusing to start",
                 WIFI_AP_MIN_PASSWORD_LENGTH);

        if (ap_flag_set && !wifi_clear_ap_fallback_flag(state)) {
            ESP_LOGW(TAG, "Failed to clear AP fallback flag after password validation error");
        }
        wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_FAILED);
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy((char *)ap_config.ap.password, ap_password, sizeof(ap_config.ap.password));
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.pmf_cfg = (wifi_pmf_config_t){
        .capable = true,
        .required = false,
    };

    ESP_LOGW(TAG, "Starting Wi-Fi fallback access point '%s'", ap_config.ap.ssid);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_schedule_sta_retry(state, WIFI_AP_STA_RETRY_INTERVAL_MS);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Wi-Fi connection failed and AP fallback disabled");
    wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_FAILED);
    return ESP_FAIL;
#endif
}

void wifi_state_machine_retry_timer_callback(TimerHandle_t timer)
{
    if (timer == NULL) {
        return;
    }

    wifi_shared_state_t *state = (wifi_shared_state_t *)pvTimerGetTimerID(timer);
    if (state == NULL) {
        return;
    }

    bool fallback_active = false;
    if (wifi_state_lock(state, pdMS_TO_TICKS(25))) {
        fallback_active = state->ap_fallback_active;
        wifi_state_unlock(state);
    }

    if (!fallback_active) {
        return;
    }

    ESP_LOGI(TAG, "STA retry timer fired while fallback AP active");
    ESP_LOGI(TAG, "Retrying STA connection while fallback AP is active");
    wifi_state_machine_start_sta(state);

    if (wifi_state_lock(state, pdMS_TO_TICKS(25))) {
        fallback_active = state->ap_fallback_active;
        wifi_state_unlock(state);
    }

    if (fallback_active) {
        wifi_schedule_sta_retry(state, WIFI_AP_STA_RETRY_INTERVAL_MS);
    }
}

esp_err_t wifi_state_machine_register_event_handlers(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_state_machine_event_handler,
                                                         state,
                                                         &state->wifi_event_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &wifi_state_machine_event_handler,
                                              state,
                                              &state->ip_got_handle);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, state->wifi_event_handle);
        state->wifi_event_handle = NULL;
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_LOST_IP,
                                              &wifi_state_machine_event_handler,
                                              state,
                                              &state->ip_lost_handle);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, state->ip_got_handle);
        state->ip_got_handle = NULL;
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, state->wifi_event_handle);
        state->wifi_event_handle = NULL;
    }

    return err;
}

void wifi_state_machine_unregister_event_handlers(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->wifi_event_handle != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, state->wifi_event_handle);
        state->wifi_event_handle = NULL;
    }
    if (state->ip_got_handle != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, state->ip_got_handle);
        state->ip_got_handle = NULL;
    }
    if (state->ip_lost_handle != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, state->ip_lost_handle);
        state->ip_lost_handle = NULL;
    }
}
#endif

void wifi_state_machine_start_sta(wifi_shared_state_t *state)
{
    if (state == NULL) {
        return;
    }

#if CONFIG_TINYBMS_WIFI_ENABLE
#ifdef ESP_PLATFORM
    if (!state->initialised) {
        ESP_LOGW(TAG, "Ignoring request to start STA mode: Wi-Fi not initialised");
        return;
    }

    wifi_stop_sta_retry_timer(state);

    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        if (state->ap_fallback_active) {
            ESP_LOGI(TAG, "Stopping fallback AP to retry STA connection");
        }
        state->ap_fallback_active = false;
        state->retry_count = 0;
        wifi_state_unlock(state);
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "esp_wifi_stop before STA restart returned %s", esp_err_to_name(err));
    }

    wifi_state_machine_configure_sta(state);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi station mode: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi station mode started");
#else
    ESP_LOGI(TAG, "Wi-Fi station mode start requested (host build stub)");
#endif
#else
    ESP_LOGI(TAG, "Wi-Fi support disabled, station mode start ignored");
#endif
}

static void wifi_handle_sta_start(wifi_shared_state_t *state)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_STA_START);
#ifdef ESP_PLATFORM
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    ESP_LOGI(TAG, "Wi-Fi station started, connecting to '%s'", settings->sta.ssid);
    wifi_attempt_connect();
#endif
}

static void wifi_handle_sta_connected(wifi_shared_state_t *state)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_STA_CONNECTED);
#ifdef ESP_PLATFORM
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    ESP_LOGI(TAG, "Wi-Fi connected to '%s'", settings->sta.ssid);
    wifi_stop_sta_retry_timer(state);
    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        state->retry_count = 0;
        wifi_state_unlock(state);
    }
#endif
}

static void wifi_handle_sta_disconnected(wifi_shared_state_t *state, const void *event_data)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_STA_DISCONNECTED);
#ifdef ESP_PLATFORM
    const config_manager_wifi_settings_t *settings = wifi_get_settings();
    uint8_t max_retry = settings->sta.max_retry > 0 ? settings->sta.max_retry : CONFIG_TINYBMS_WIFI_STA_MAX_RETRY;

    int reason = -1;
    if (event_data != NULL) {
        const wifi_state_disconnected_info_t *info = (const wifi_state_disconnected_info_t *)event_data;
        reason = info->reason;
    }
    ESP_LOGW(TAG, "Station disconnected, reason=%d", reason);

    bool ap_active = false;
    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        ap_active = state->ap_fallback_active;
        wifi_state_unlock(state);
    }

    if (ap_active) {
        ESP_LOGW(TAG, "Station disconnected while fallback AP active");
        return;
    }

    int current_retry = 0;
    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        state->retry_count++;
        current_retry = state->retry_count;
        wifi_state_unlock(state);
    } else {
        ESP_LOGW(TAG, "Failed to acquire wifi state mutex");
        return;
    }

    if (current_retry < max_retry) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", current_retry, max_retry);
        wifi_attempt_connect();
    } else {
        ESP_LOGE(TAG, "Wi-Fi failed to connect after %d attempts", max_retry);
#if CONFIG_TINYBMS_WIFI_AP_FALLBACK
        wifi_state_machine_start_fallback_ap(state);
#else
        if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
            state->retry_count++;
            int retry = state->retry_count;
            wifi_state_unlock(state);

            uint32_t backoff_ms = 1000U << (retry < 6 ? retry : 6);
            if (backoff_ms > 60000U) {
                backoff_ms = 60000U;
            }

            ESP_LOGW(TAG, "Retry %d in %u ms", retry, backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            wifi_attempt_connect();
        } else {
            ESP_LOGE(TAG, "Cannot acquire mutex for retry logic");
        }
#endif
    }
#endif
}

static void wifi_handle_sta_got_ip(wifi_shared_state_t *state, const void *event_data)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_STA_GOT_IP);
#ifdef ESP_PLATFORM
    wifi_stop_sta_retry_timer(state);

    if (wifi_state_lock(state, pdMS_TO_TICKS(100))) {
        state->retry_count = 0;
        state->ap_fallback_active = false;
        wifi_state_unlock(state);
    }

    if (event_data != NULL) {
        const ip_event_got_ip_t *ip_event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi station obtained IP address: %s", ip4addr_ntoa(&ip_event->ip_info.ip));
    } else {
        ESP_LOGI(TAG, "Wi-Fi station obtained IP address");
    }
#endif
}

static void wifi_handle_sta_lost_ip(wifi_shared_state_t *state)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_STA_LOST_IP);
#ifdef ESP_PLATFORM
    ESP_LOGW(TAG, "Wi-Fi station lost IP address");
#endif
}

static void wifi_handle_ap_started(wifi_shared_state_t *state)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_STARTED);
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Wi-Fi access point started");
    wifi_schedule_sta_retry(state, WIFI_AP_STA_RETRY_INTERVAL_MS);
#endif
}

static void wifi_handle_ap_stopped(wifi_shared_state_t *state)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_STOPPED);
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Wi-Fi access point stopped");
    wifi_stop_sta_retry_timer(state);
#endif
}

static void wifi_handle_ap_client_connected(wifi_shared_state_t *state, const void *event_data)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED);
#ifdef ESP_PLATFORM
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
#endif
}

static void wifi_handle_ap_client_disconnected(wifi_shared_state_t *state, const void *event_data)
{
    wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED);
#ifdef ESP_PLATFORM
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
#endif
}

void wifi_state_machine_process_transition(wifi_shared_state_t *state,
                                           wifi_state_transition_t transition,
                                           const void *event_data)
{
    switch (transition) {
        case WIFI_STATE_TRANSITION_STA_START:
            wifi_handle_sta_start(state);
            break;
        case WIFI_STATE_TRANSITION_STA_CONNECTED:
            wifi_handle_sta_connected(state);
            break;
        case WIFI_STATE_TRANSITION_STA_DISCONNECTED:
            wifi_handle_sta_disconnected(state, event_data);
            break;
        case WIFI_STATE_TRANSITION_STA_GOT_IP:
            wifi_handle_sta_got_ip(state, event_data);
            break;
        case WIFI_STATE_TRANSITION_STA_LOST_IP:
            wifi_handle_sta_lost_ip(state);
            break;
        case WIFI_STATE_TRANSITION_AP_STARTED:
            wifi_handle_ap_started(state);
            break;
        case WIFI_STATE_TRANSITION_AP_STOPPED:
            wifi_handle_ap_stopped(state);
            break;
        case WIFI_STATE_TRANSITION_AP_FAILED:
            wifi_events_publish(state, APP_EVENT_ID_WIFI_AP_FAILED);
#ifdef ESP_PLATFORM
            ESP_LOGW(TAG, "Fallback AP start failed");
#endif
            break;
        case WIFI_STATE_TRANSITION_AP_CLIENT_CONNECTED:
            wifi_handle_ap_client_connected(state, event_data);
            break;
        case WIFI_STATE_TRANSITION_AP_CLIENT_DISCONNECTED:
            wifi_handle_ap_client_disconnected(state, event_data);
            break;
        default:
            break;
    }
}

#ifdef ESP_PLATFORM
void wifi_state_machine_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_shared_state_t *state = (wifi_shared_state_t *)ctx;
    if (state == NULL) {
        return;
    }

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_STA_START, NULL);
                break;
            case WIFI_EVENT_STA_CONNECTED:
                wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_STA_CONNECTED, NULL);
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_state_disconnected_info_t info = {.reason = -1};
                if (event_data != NULL) {
                    const wifi_event_sta_disconnected_t *raw = (const wifi_event_sta_disconnected_t *)event_data;
                    info.reason = raw->reason;
                }
                wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_STA_DISCONNECTED, &info);
                break;
            }
            case WIFI_EVENT_AP_START:
                wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_AP_STARTED, NULL);
                break;
            case WIFI_EVENT_AP_STOP:
                wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_AP_STOPPED, NULL);
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                wifi_state_machine_process_transition(state,
                                                      WIFI_STATE_TRANSITION_AP_CLIENT_CONNECTED,
                                                      event_data);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                wifi_state_machine_process_transition(state,
                                                      WIFI_STATE_TRANSITION_AP_CLIENT_DISCONNECTED,
                                                      event_data);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_STA_GOT_IP, event_data);
        } else if (event_id == IP_EVENT_STA_LOST_IP) {
            wifi_state_machine_process_transition(state, WIFI_STATE_TRANSITION_STA_LOST_IP, NULL);
        }
    }
}
#endif
