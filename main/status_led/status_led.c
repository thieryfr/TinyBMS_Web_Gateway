#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_events.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define STATUS_LED_GPIO GPIO_NUM_2
#define STATUS_LED_QUEUE_LENGTH 16
#define STATUS_LED_TASK_STACK_SIZE 2048
#define STATUS_LED_EVENT_TASK_STACK_SIZE 3072
#define STATUS_LED_TASK_PRIORITY 5
#define STATUS_LED_EVENT_TASK_PRIORITY 4
#define STATUS_LED_ACTIVITY_PULSE_MS 100U
#define STATUS_LED_OTA_WINDOW_MS 30000U

typedef enum {
    STATUS_LED_SYSTEM_MODE_BOOT = 0,
    STATUS_LED_SYSTEM_MODE_READY,
} status_led_system_mode_t;

typedef enum {
    STATUS_LED_WIFI_STATUS_UNKNOWN = 0,
    STATUS_LED_WIFI_STATUS_CONNECTING,
    STATUS_LED_WIFI_STATUS_CONNECTED,
    STATUS_LED_WIFI_STATUS_AP,
} status_led_wifi_status_t;

typedef enum {
    STATUS_LED_STORAGE_STATUS_UNKNOWN = 0,
    STATUS_LED_STORAGE_STATUS_READY,
    STATUS_LED_STORAGE_STATUS_UNAVAILABLE,
} status_led_storage_status_t;

typedef enum {
    STATUS_LED_COMMAND_SET_SYSTEM_MODE = 0,
    STATUS_LED_COMMAND_SET_WIFI_STATUS,
    STATUS_LED_COMMAND_SET_STORAGE_STATUS,
    STATUS_LED_COMMAND_SET_OTA_WINDOW,
    STATUS_LED_COMMAND_ACTIVITY_PULSE,
} status_led_command_type_t;

typedef struct {
    status_led_command_type_t type;
    union {
        status_led_system_mode_t system_mode;
        status_led_wifi_status_t wifi_status;
        status_led_storage_status_t storage_status;
        TickType_t duration_ticks;
    } data;
} status_led_command_t;

typedef struct {
    status_led_system_mode_t system_mode;
    status_led_wifi_status_t wifi_status;
    status_led_storage_status_t storage_status;
    TickType_t ota_deadline;
    TickType_t activity_deadline;
    TickType_t pattern_reference_tick;
} status_led_state_t;

static const char *TAG = "status_led";

static QueueHandle_t s_command_queue = NULL;
static event_bus_subscription_handle_t s_event_subscription = NULL;
static TaskHandle_t s_led_task_handle = NULL;
static TaskHandle_t s_event_task_handle = NULL;
static volatile bool s_task_should_exit = false;
static bool s_initialised = false;

static bool status_led_is_time_past(TickType_t now, TickType_t deadline)
{
    return ((int32_t)(now - deadline)) >= 0;
}

static uint32_t status_led_ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)((uint64_t)ticks * (uint64_t)portTICK_PERIOD_MS);
}

static bool status_led_compute_storage_pattern(const status_led_state_t *state, TickType_t now)
{
    uint32_t elapsed_ms = status_led_ticks_to_ms(now - state->pattern_reference_tick);
    uint32_t phase = elapsed_ms % 1300U;
    return (phase < 100U) || (phase >= 200U && phase < 300U);
}

static bool status_led_compute_boot_pattern(const status_led_state_t *state, TickType_t now)
{
    uint32_t elapsed_ms = status_led_ticks_to_ms(now - state->pattern_reference_tick);
    return ((elapsed_ms / 125U) & 1U) == 0U;
}

static bool status_led_compute_slow_blink(const status_led_state_t *state, TickType_t now)
{
    uint32_t elapsed_ms = status_led_ticks_to_ms(now - state->pattern_reference_tick);
    return ((elapsed_ms / 500U) & 1U) == 0U;
}

static bool status_led_compute_ap_pattern(const status_led_state_t *state, TickType_t now)
{
    uint32_t elapsed_ms = status_led_ticks_to_ms(now - state->pattern_reference_tick);
    return ((elapsed_ms / 1000U) & 1U) == 0U;
}

static bool status_led_compute_ota_pattern(const status_led_state_t *state, TickType_t now)
{
    uint32_t elapsed_ms = status_led_ticks_to_ms(now - state->pattern_reference_tick);
    return ((elapsed_ms / 250U) & 1U) == 0U;
}

static bool status_led_compute_level(const status_led_state_t *state, TickType_t now)
{
    if (state->storage_status == STATUS_LED_STORAGE_STATUS_UNAVAILABLE) {
        return status_led_compute_storage_pattern(state, now);
    }

    if (state->ota_deadline != 0 && !status_led_is_time_past(now, state->ota_deadline)) {
        return status_led_compute_ota_pattern(state, now);
    }

    if (state->system_mode == STATUS_LED_SYSTEM_MODE_BOOT) {
        return status_led_compute_boot_pattern(state, now);
    }

    if (state->wifi_status == STATUS_LED_WIFI_STATUS_CONNECTED) {
        if (state->activity_deadline != 0 && !status_led_is_time_past(now, state->activity_deadline)) {
            return false;
        }
        return true;
    }

    if (state->wifi_status == STATUS_LED_WIFI_STATUS_AP) {
        return status_led_compute_ap_pattern(state, now);
    }

    return status_led_compute_slow_blink(state, now);
}

static void status_led_send_command(const status_led_command_t *command)
{
    if (s_command_queue == NULL) {
        return;
    }

    if (xQueueSend(s_command_queue, command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Dropping LED command %d", (int)command->type);
    }
}

static void status_led_handle_event(const event_bus_event_t *event)
{
    switch (event->id) {
    case APP_EVENT_ID_WIFI_STA_START:
    case APP_EVENT_ID_WIFI_STA_CONNECTED:
    case APP_EVENT_ID_WIFI_STA_DISCONNECTED:
    case APP_EVENT_ID_WIFI_STA_LOST_IP: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_WIFI_STATUS,
            .data.wifi_status = STATUS_LED_WIFI_STATUS_CONNECTING,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_WIFI_STA_GOT_IP: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_WIFI_STATUS,
            .data.wifi_status = STATUS_LED_WIFI_STATUS_CONNECTED,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_WIFI_AP_STARTED: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_WIFI_STATUS,
            .data.wifi_status = STATUS_LED_WIFI_STATUS_AP,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_WIFI_AP_STOPPED: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_WIFI_STATUS,
            .data.wifi_status = STATUS_LED_WIFI_STATUS_CONNECTING,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_STORAGE_HISTORY_READY: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_STORAGE_STATUS,
            .data.storage_status = STATUS_LED_STORAGE_STATUS_READY,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_STORAGE_HISTORY_UNAVAILABLE: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_STORAGE_STATUS,
            .data.storage_status = STATUS_LED_STORAGE_STATUS_UNAVAILABLE,
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_OTA_UPLOAD_READY: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_SET_OTA_WINDOW,
            .data.duration_ticks = pdMS_TO_TICKS(STATUS_LED_OTA_WINDOW_MS),
        };
        status_led_send_command(&cmd);
        break;
    }
    case APP_EVENT_ID_CAN_FRAME_RAW:
    case APP_EVENT_ID_CAN_FRAME_DECODED:
    case APP_EVENT_ID_CAN_FRAME_READY:
    case APP_EVENT_ID_UART_FRAME_RAW:
    case APP_EVENT_ID_UART_FRAME_DECODED:
    case APP_EVENT_ID_BMS_LIVE_DATA:
    case APP_EVENT_ID_TELEMETRY_SAMPLE:
    case APP_EVENT_ID_MQTT_METRICS:
    case APP_EVENT_ID_UI_NOTIFICATION:
    case APP_EVENT_ID_WIFI_AP_CLIENT_CONNECTED:
    case APP_EVENT_ID_WIFI_AP_CLIENT_DISCONNECTED: {
        status_led_command_t cmd = {
            .type = STATUS_LED_COMMAND_ACTIVITY_PULSE,
        };
        status_led_send_command(&cmd);
        break;
    }
    default:
        break;
    }
}

static void status_led_event_task(void *ctx)
{
    event_bus_subscription_handle_t subscription = (event_bus_subscription_handle_t)ctx;

    while (subscription != NULL && !s_task_should_exit) {
        event_bus_event_t event;
        if (!event_bus_receive(subscription, &event, pdMS_TO_TICKS(100))) {
            continue;
        }
        status_led_handle_event(&event);
    }

    ESP_LOGI(TAG, "Event task exiting");
    vTaskDelete(NULL);
}

static void status_led_handle_command(status_led_state_t *state, const status_led_command_t *command)
{
    TickType_t now = xTaskGetTickCount();

    switch (command->type) {
    case STATUS_LED_COMMAND_SET_SYSTEM_MODE:
        if (state->system_mode != command->data.system_mode) {
            state->system_mode = command->data.system_mode;
            state->pattern_reference_tick = now;
        }
        break;
    case STATUS_LED_COMMAND_SET_WIFI_STATUS:
        if (state->wifi_status != command->data.wifi_status) {
            state->wifi_status = command->data.wifi_status;
            state->pattern_reference_tick = now;
        }
        break;
    case STATUS_LED_COMMAND_SET_STORAGE_STATUS:
        if (state->storage_status != command->data.storage_status) {
            state->storage_status = command->data.storage_status;
            state->pattern_reference_tick = now;
        }
        break;
    case STATUS_LED_COMMAND_SET_OTA_WINDOW:
        state->ota_deadline = (command->data.duration_ticks == 0)
                                  ? 0
                                  : now + command->data.duration_ticks;
        state->pattern_reference_tick = now;
        break;
    case STATUS_LED_COMMAND_ACTIVITY_PULSE:
        state->activity_deadline = now + pdMS_TO_TICKS(STATUS_LED_ACTIVITY_PULSE_MS);
        break;
    default:
        break;
    }
}

static void status_led_task(void *ctx)
{
    (void)ctx;

    status_led_state_t state = {
        .system_mode = STATUS_LED_SYSTEM_MODE_BOOT,
        .wifi_status = STATUS_LED_WIFI_STATUS_UNKNOWN,
        .storage_status = STATUS_LED_STORAGE_STATUS_UNKNOWN,
        .ota_deadline = 0,
        .activity_deadline = 0,
        .pattern_reference_tick = xTaskGetTickCount(),
    };

    while (!s_task_should_exit) {
        status_led_command_t command;
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(20)) == pdTRUE) {
            status_led_handle_command(&state, &command);
            while (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
                status_led_handle_command(&state, &command);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (state.ota_deadline != 0 && status_led_is_time_past(now, state.ota_deadline)) {
            state.ota_deadline = 0;
            state.pattern_reference_tick = now;
        }
        if (state.activity_deadline != 0 && status_led_is_time_past(now, state.activity_deadline)) {
            state.activity_deadline = 0;
        }

        bool level = status_led_compute_level(&state, now);
        gpio_set_level(STATUS_LED_GPIO, level ? 1 : 0);
    }

    ESP_LOGI(TAG, "LED task exiting");
    vTaskDelete(NULL);
}

void status_led_init(void)
{
    if (s_initialised) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure status LED GPIO");
        return;
    }

    gpio_set_level(STATUS_LED_GPIO, 0);

    s_command_queue = xQueueCreate(STATUS_LED_QUEUE_LENGTH, sizeof(status_led_command_t));
    if (s_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to allocate command queue");
        return;
    }

    s_event_subscription = event_bus_subscribe_default(NULL, NULL);
    if (s_event_subscription == NULL) {
        ESP_LOGE(TAG, "Failed to subscribe to event bus");
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return;
    }

    if (xTaskCreate(status_led_task, "status_led", STATUS_LED_TASK_STACK_SIZE, NULL, STATUS_LED_TASK_PRIORITY, &s_led_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status LED task");
        event_bus_unsubscribe(s_event_subscription);
        s_event_subscription = NULL;
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return;
    }

    if (xTaskCreate(status_led_event_task,
                    "status_led_evt",
                    STATUS_LED_EVENT_TASK_STACK_SIZE,
                    (void *)s_event_subscription,
                    STATUS_LED_EVENT_TASK_PRIORITY,
                    &s_event_task_handle)
        != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status LED event task");
        // Continue without event-driven updates; manual commands still work.
    }

    s_task_should_exit = false;
    s_initialised = true;
}

void status_led_notify_system_ready(void)
{
    if (!s_initialised) {
        return;
    }

    status_led_command_t cmd = {
        .type = STATUS_LED_COMMAND_SET_SYSTEM_MODE,
        .data.system_mode = STATUS_LED_SYSTEM_MODE_READY,
    };
    status_led_send_command(&cmd);
}

void status_led_deinit(void)
{
    if (!s_initialised) {
        ESP_LOGW(TAG, "Already deinitialized");
        return;
    }

    ESP_LOGI(TAG, "Deinitializing status LED controller...");

    // Signal tasks to exit
    s_task_should_exit = true;

    // Give tasks time to exit gracefully
    vTaskDelay(pdMS_TO_TICKS(200));

    // Turn off LED
    gpio_set_level(STATUS_LED_GPIO, 0);

    // Unsubscribe from event bus
    if (s_event_subscription != NULL) {
        event_bus_unsubscribe(s_event_subscription);
        s_event_subscription = NULL;
    }

    // Delete command queue
    if (s_command_queue != NULL) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
    }

    // Reset task handles
    s_led_task_handle = NULL;
    s_event_task_handle = NULL;

    s_initialised = false;
    ESP_LOGI(TAG, "Status LED controller deinitialized");
}

