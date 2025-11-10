#include "system_control.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "uart_bms.h"

#ifndef SYSTEM_CONTROL_BMS_RESTART_GUARD_MS
#define SYSTEM_CONTROL_BMS_RESTART_GUARD_MS 5000U
#endif

#ifndef SYSTEM_CONTROL_GATEWAY_RESTART_DELAY_MS
#define SYSTEM_CONTROL_GATEWAY_RESTART_DELAY_MS 750U
#endif

static const char *TAG = "sys_control";

static SemaphoreHandle_t s_bms_mutex = NULL;
static TickType_t s_last_bms_restart = 0;
static esp_timer_handle_t s_restart_timer = NULL;

static SemaphoreHandle_t system_control_get_mutex(void)
{
    if (s_bms_mutex == NULL) {
        s_bms_mutex = xSemaphoreCreateMutex();
    }
    return s_bms_mutex;
}

static void system_control_restart_callback(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Gateway restart requested via REST API");
    esp_restart();
}

esp_err_t system_control_request_bms_restart(uint32_t timeout_ms)
{
    SemaphoreHandle_t mutex = system_control_get_mutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    TickType_t guard = pdMS_TO_TICKS(SYSTEM_CONTROL_BMS_RESTART_GUARD_MS);
    if (guard == 0) {
        guard = 1;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_last_bms_restart != 0 && (now - s_last_bms_restart) < guard) {
        xSemaphoreGive(mutex);
        ESP_LOGW(TAG, "BMS restart request throttled - already sent %u ms ago", (unsigned)((now - s_last_bms_restart) * portTICK_PERIOD_MS));
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = uart_bms_request_restart(timeout_ms);
    if (err == ESP_OK) {
        s_last_bms_restart = now;
        ESP_LOGI(TAG, "TinyBMS restart command sent over UART");
    } else {
        ESP_LOGW(TAG, "TinyBMS restart command failed: %s", esp_err_to_name(err));
    }

    xSemaphoreGive(mutex);
    return err;
}

esp_err_t system_control_schedule_gateway_restart(uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        delay_ms = SYSTEM_CONTROL_GATEWAY_RESTART_DELAY_MS;
    }

    if (s_restart_timer != NULL) {
        esp_err_t stop_err = esp_timer_stop(s_restart_timer);
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop previous restart timer: %s", esp_err_to_name(stop_err));
        }
    }

    if (s_restart_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &system_control_restart_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "gateway_restart",
        };
        esp_err_t create_err = esp_timer_create(&args, &s_restart_timer);
        if (create_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create restart timer: %s", esp_err_to_name(create_err));
            return create_err;
        }
    }

    esp_err_t err = esp_timer_start_once(s_restart_timer, (uint64_t)delay_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start restart timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Gateway restart scheduled in %u ms", (unsigned)delay_ms);
    return ESP_OK;
}

