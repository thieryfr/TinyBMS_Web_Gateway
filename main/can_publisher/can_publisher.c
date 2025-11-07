#include "can_publisher.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <sys/time.h>
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "app_events.h"
#include "config_manager.h"
#include "conversion_table.h"
#include "cvl_controller.h"
#include "can_config_defaults.h"

#define CAN_PUBLISHER_EVENT_TIMEOUT_MS 50U
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS  20U

// CAN configuration defaults are now centralized in can_config_defaults.h

#ifndef CONFIG_TINYBMS_CAN_SERIAL_NUMBER
#define CONFIG_TINYBMS_CAN_SERIAL_NUMBER "TinyBMS-00000000"
#endif

static const char *TAG = "can_pub";

static event_bus_publish_fn_t s_event_publisher = NULL;
static can_publisher_frame_publish_fn_t s_frame_publisher = NULL;
static can_publisher_buffer_t s_frame_buffer = {
    .slots = {0},
    .slot_valid = {0},
    .capacity = 0,
};
static can_publisher_registry_t s_registry = {
    .channels = NULL,
    .channel_count = 0,
    .buffer = &s_frame_buffer,
};
static SemaphoreHandle_t s_buffer_mutex = NULL;
static SemaphoreHandle_t s_event_mutex = NULL;  // Replaced portMUX_TYPE for consistent synchronization
static TaskHandle_t s_publish_task_handle = NULL;
static bool s_listener_registered = false;
static uint32_t s_publish_interval_ms = CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS;
static can_publisher_frame_t s_event_frames[CAN_PUBLISHER_MAX_BUFFER_SLOTS];
static size_t s_event_frame_index = 0;
static TickType_t s_channel_period_ticks[CAN_PUBLISHER_MAX_BUFFER_SLOTS] = {0};
static TickType_t s_channel_deadlines[CAN_PUBLISHER_MAX_BUFFER_SLOTS] = {0};

static const config_manager_can_settings_t *can_publisher_get_settings(void);
static bool can_publisher_periodic_mode_enabled(void);
static TickType_t can_publisher_publish_buffer(can_publisher_registry_t *registry, TickType_t now);
static bool can_publisher_store_frame(can_publisher_buffer_t *buffer,
                                      size_t index,
                                      const can_publisher_frame_t *frame);
static void can_publisher_task(void *context);

static TickType_t can_publisher_ms_to_ticks(uint32_t period_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(period_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

static uint64_t can_publisher_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

void can_publisher_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

static void can_publisher_publish_event(const can_publisher_frame_t *frame)
{
    if (s_event_publisher == NULL || frame == NULL) {
        return;
    }

    if (s_event_mutex == NULL) {
        ESP_LOGW(TAG, "Event mutex not initialized");
        return;
    }

    can_publisher_frame_t *event_frame = NULL;

    // Use mutex instead of spinlock for consistent synchronization
    if (xSemaphoreTake(s_event_mutex, pdMS_TO_TICKS(CAN_PUBLISHER_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire event mutex");
        return;
    }

    size_t slot = s_event_frame_index;
    s_event_frame_index = (s_event_frame_index + 1U) % CAN_PUBLISHER_MAX_BUFFER_SLOTS;
    s_event_frames[slot] = *frame;
    event_frame = &s_event_frames[slot];

    xSemaphoreGive(s_event_mutex);

    event_bus_event_t event = {
        .id = APP_EVENT_ID_CAN_FRAME_READY,
        .payload = event_frame,
        .payload_size = sizeof(*event_frame),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(CAN_PUBLISHER_EVENT_TIMEOUT_MS))) {
        ESP_LOGW(TAG, "Failed to publish CAN frame event for ID 0x%08" PRIX32, frame->id);
    }
}

static void can_publisher_dispatch_frame(const can_publisher_channel_t *channel,
                                         const can_publisher_frame_t *frame)
{
    if (channel == NULL || frame == NULL) {
        return;
    }

    if (s_frame_publisher != NULL) {
        esp_err_t err = s_frame_publisher(channel->can_id, frame->data, frame->dlc, channel->description);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to publish CAN frame 0x%08" PRIX32 ": %s",
                     channel->can_id,
                     esp_err_to_name(err));
        }
    }

    can_publisher_publish_event(frame);
}

void can_publisher_init(event_bus_publish_fn_t publisher,
                        can_publisher_frame_publish_fn_t frame_publisher)
{
    can_publisher_set_event_publisher(publisher);
    s_frame_publisher = frame_publisher;

    can_publisher_cvl_init();

    const config_manager_can_settings_t *settings = can_publisher_get_settings();
    s_publish_interval_ms = settings->publisher.period_ms;

    s_registry.channels = g_can_publisher_channels;
    s_registry.channel_count = g_can_publisher_channel_count;

    if (s_registry.channel_count > CAN_PUBLISHER_MAX_BUFFER_SLOTS) {
        ESP_LOGW(TAG,
                 "Configured %zu CAN channels exceeds buffer capacity (%u), truncating",
                 s_registry.channel_count,
                 (unsigned)CAN_PUBLISHER_MAX_BUFFER_SLOTS);
        s_registry.channel_count = CAN_PUBLISHER_MAX_BUFFER_SLOTS;
    }

    s_frame_buffer.capacity = s_registry.channel_count;
    memset(s_frame_buffer.slots, 0, sizeof(s_frame_buffer.slots));
    memset(s_frame_buffer.slot_valid, 0, sizeof(s_frame_buffer.slot_valid));
    memset(s_event_frames, 0, sizeof(s_event_frames));
    s_event_frame_index = 0;

    TickType_t now_ticks = xTaskGetTickCount();
    for (size_t i = 0; i < s_registry.channel_count; ++i) {
        const can_publisher_channel_t *channel = &s_registry.channels[i];
        uint32_t period_ms = channel->period_ms;
        if (period_ms == 0U) {
            period_ms = (s_publish_interval_ms > 0U) ? s_publish_interval_ms : 1000U;
        }
        s_channel_period_ticks[i] = can_publisher_ms_to_ticks(period_ms);
        s_channel_deadlines[i] = now_ticks;
        ESP_LOGI(TAG,
                 "Channel %zu PGN 0x%03X scheduled every %u ms",
                 i,
                 (unsigned)channel->pgn,
                 (unsigned)period_ms);
    }

    if (s_buffer_mutex == NULL) {
        s_buffer_mutex = xSemaphoreCreateMutex();
        if (s_buffer_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create CAN publisher buffer mutex");
        }
    }

    if (s_event_mutex == NULL) {
        s_event_mutex = xSemaphoreCreateMutex();
        if (s_event_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create CAN publisher event mutex");
        }
    }

    esp_err_t err = uart_bms_register_listener(can_publisher_on_bms_update, &s_registry);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(err));
    } else {
        s_listener_registered = true;
        ESP_LOGI(TAG, "CAN publisher initialised with %zu channels", s_registry.channel_count);
    }

    if (can_publisher_periodic_mode_enabled() && s_publish_task_handle == NULL) {
        BaseType_t task_created = xTaskCreate(can_publisher_task,
                                             "can_pub",
                                             3072,
                                             &s_registry,
                                             tskIDLE_PRIORITY + 2,
                                             &s_publish_task_handle);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to start CAN publisher task");
            s_publish_task_handle = NULL;
            ESP_LOGW(TAG, "Falling back to immediate CAN frame dispatch");
        } else {
            ESP_LOGI(TAG,
                     "CAN publisher task running with %u ms interval",
                     (unsigned)s_publish_interval_ms);
        }
    } else if (!can_publisher_periodic_mode_enabled()) {
        ESP_LOGI(TAG, "CAN publisher dispatching immediately on TinyBMS updates");
    }
}

void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    can_publisher_registry_t *registry = (can_publisher_registry_t *)context;

    if (data == NULL || registry == NULL || registry->channels == NULL || registry->buffer == NULL) {
        return;
    }

    if (registry->channel_count == 0 || registry->buffer->capacity == 0) {
        return;
    }

    can_publisher_cvl_prepare(data);

    uint64_t timestamp_ms = (data->timestamp_ms > 0U) ? data->timestamp_ms : can_publisher_timestamp_ms();

    bool periodic = can_publisher_periodic_mode_enabled() && (s_publish_task_handle != NULL);

    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];

        if (channel->fill_fn == NULL) {
            continue;
        }

        can_publisher_frame_t frame = {
            .id = channel->can_id,
            .dlc = (channel->dlc > 8U) ? 8U : channel->dlc,
            .data = {0},
            .timestamp_ms = timestamp_ms,
        };

        bool encoded = channel->fill_fn(data, &frame);
        if (!encoded) {
            ESP_LOGW(TAG, "Encoder rejected TinyBMS sample for CAN ID 0x%08" PRIX32, channel->can_id);
            continue;
        }

        bool stored = can_publisher_store_frame(registry->buffer, i, &frame);
        if (!stored) {
            continue;
        }

        if (!periodic) {
            can_publisher_dispatch_frame(channel, &frame);
        }
    }
}

void can_publisher_deinit(void)
{
    // Unregister listener first to stop new data from coming in
    if (s_listener_registered) {
        uart_bms_unregister_listener(can_publisher_on_bms_update, &s_registry);
        s_listener_registered = false;
    }

    // Allow task to complete current cycle before deletion
    if (s_publish_task_handle != NULL) {
        // Give the task time to finish its current iteration
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(s_publish_task_handle);
        s_publish_task_handle = NULL;
    }

    if (s_buffer_mutex != NULL) {
        vSemaphoreDelete(s_buffer_mutex);
        s_buffer_mutex = NULL;
    }

    if (s_event_mutex != NULL) {
        vSemaphoreDelete(s_event_mutex);
        s_event_mutex = NULL;
    }

    memset(&s_frame_buffer, 0, sizeof(s_frame_buffer));
    s_registry.buffer = &s_frame_buffer;
    s_registry.channel_count = 0;
    s_registry.channels = NULL;

    memset(s_event_frames, 0, sizeof(s_event_frames));
    s_event_frame_index = 0;
    memset(s_channel_period_ticks, 0, sizeof(s_channel_period_ticks));
    memset(s_channel_deadlines, 0, sizeof(s_channel_deadlines));

    const config_manager_can_settings_t *settings = can_publisher_get_settings();
    s_publish_interval_ms = settings->publisher.period_ms;

    s_frame_publisher = NULL;
    s_event_publisher = NULL;

    can_publisher_cvl_init();
}

static bool can_publisher_periodic_mode_enabled(void)
{
    return (s_publish_interval_ms > 0U);
}

static bool can_publisher_store_frame(can_publisher_buffer_t *buffer,
                                      size_t index,
                                      const can_publisher_frame_t *frame)
{
    if (buffer == NULL || frame == NULL || index >= buffer->capacity) {
        return false;
    }

    if (s_buffer_mutex != NULL) {
        if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(CAN_PUBLISHER_LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Timed out acquiring CAN publisher buffer lock");
            return false;
        }

        buffer->slots[index] = *frame;
        buffer->slot_valid[index] = true;
        s_channel_deadlines[index] = xTaskGetTickCount();
        xSemaphoreGive(s_buffer_mutex);
        return true;
    }

    buffer->slots[index] = *frame;
    buffer->slot_valid[index] = true;
    s_channel_deadlines[index] = xTaskGetTickCount();
    return true;
}

static TickType_t can_publisher_publish_buffer(can_publisher_registry_t *registry, TickType_t now)
{
    if (registry == NULL || registry->channels == NULL || registry->buffer == NULL) {
        return 1;
    }

    can_publisher_buffer_t *buffer = registry->buffer;

    if (buffer->capacity == 0) {
        return 1;
    }

    TickType_t next_delay = 0;
    bool have_delay = false;

    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];
        can_publisher_frame_t frame = {0};
        bool has_frame = false;

        if (s_buffer_mutex != NULL) {
            if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(CAN_PUBLISHER_LOCK_TIMEOUT_MS)) == pdTRUE) {
                if (buffer->slot_valid[i]) {
                    frame = buffer->slots[i];
                    has_frame = true;
                }
                xSemaphoreGive(s_buffer_mutex);
            } else {
                ESP_LOGW(TAG, "Timed out acquiring CAN publisher buffer for read");
            }
        } else if (buffer->slot_valid[i]) {
            frame = buffer->slots[i];
            has_frame = true;
        }

        TickType_t deadline = s_channel_deadlines[i];
        if (deadline == 0) {
            deadline = now;
        }

        bool due = (now >= deadline);

        if (due && has_frame) {
            can_publisher_dispatch_frame(channel, &frame);
            s_channel_deadlines[i] = now + s_channel_period_ticks[i];
            deadline = s_channel_deadlines[i];
        } else if (due && !has_frame) {
            s_channel_deadlines[i] = now + s_channel_period_ticks[i];
            deadline = s_channel_deadlines[i];
        }

        TickType_t delta = (deadline > now) ? (deadline - now) : 0;
        if (!have_delay || delta < next_delay) {
            next_delay = delta;
            have_delay = true;
        }
    }

    if (!have_delay) {
        uint32_t default_period = (s_publish_interval_ms > 0U) ? s_publish_interval_ms : 1000U;
        next_delay = can_publisher_ms_to_ticks(default_period);
    } else if (next_delay == 0) {
        next_delay = 1;
    }

    return next_delay;
}

static void can_publisher_task(void *context)
{
    can_publisher_registry_t *registry = (can_publisher_registry_t *)context;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t delay_ticks = can_publisher_publish_buffer(registry, now);
        if (delay_ticks == 0) {
            taskYIELD();
        } else {
            vTaskDelay(delay_ticks);
        }
    }
}

static const config_manager_can_settings_t *can_publisher_get_settings(void)
{
    static const config_manager_can_settings_t defaults = {
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

    const config_manager_can_settings_t *settings = config_manager_get_can_settings();
    return (settings != NULL) ? settings : &defaults;
}

