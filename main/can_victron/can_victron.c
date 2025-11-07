#include "can_victron.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include <sys/time.h>
#endif

#include "freertos/FreeRTOS.h"

#include "config_manager.h"
#include "app_events.h"
#include "can_config_defaults.h"

#define CAN_VICTRON_EVENT_BUFFERS 4
#define CAN_VICTRON_JSON_SIZE     256

#define CAN_VICTRON_KEEPALIVE_ID         0x305U
#define CAN_VICTRON_KEEPALIVE_DLC        1U
#define CAN_VICTRON_TASK_STACK           4096
#define CAN_VICTRON_TASK_PRIORITY        (tskIDLE_PRIORITY + 6)
#define CAN_VICTRON_TASK_DELAY_MS        50U
#define CAN_VICTRON_RX_TIMEOUT_MS        10U
#define CAN_VICTRON_TX_TIMEOUT_MS        50U
#define CAN_VICTRON_LOCK_TIMEOUT_MS      50U
#define CAN_VICTRON_TWAI_TX_QUEUE_LEN    16
#define CAN_VICTRON_TWAI_RX_QUEUE_LEN    16

// CAN configuration defaults are now centralized in can_config_defaults.h

#ifndef CONFIG_TINYBMS_CAN_SERIAL_NUMBER
#define CONFIG_TINYBMS_CAN_SERIAL_NUMBER "TinyBMS-00000000"
#endif

typedef enum {
    CAN_VICTRON_DIRECTION_TX,
    CAN_VICTRON_DIRECTION_RX,
} can_victron_direction_t;

static const char *TAG = "can_victron";

static event_bus_publish_fn_t s_event_publisher = NULL;
static char s_can_raw_events[CAN_VICTRON_EVENT_BUFFERS][CAN_VICTRON_JSON_SIZE];
static char s_can_decoded_events[CAN_VICTRON_EVENT_BUFFERS][CAN_VICTRON_JSON_SIZE];
static size_t s_next_event_slot = 0;
static portMUX_TYPE s_event_slot_lock = portMUX_INITIALIZER_UNLOCKED;

#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_twai_mutex = NULL;
static SemaphoreHandle_t s_driver_state_mutex = NULL;  // Protects s_driver_started
static TaskHandle_t s_can_task_handle = NULL;
static bool s_driver_started = false;
static bool s_keepalive_ok = false;
static uint64_t s_last_keepalive_tx_ms = 0;
static uint64_t s_last_keepalive_rx_ms = 0;
static int s_twai_tx_gpio = CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO;
static int s_twai_rx_gpio = CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO;

static esp_err_t can_victron_start_driver(void);
static void can_victron_stop_driver(void);
static bool can_victron_is_driver_started(void);
static void can_victron_send_keepalive(uint64_t now);
static void can_victron_process_keepalive_rx(bool remote_request, uint64_t now);
static void can_victron_service_keepalive(uint64_t now);
static void can_victron_handle_rx_message(const twai_message_t *message);
static void can_victron_task(void *context);
#endif

static const config_manager_can_settings_t *can_victron_get_settings(void)
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

static uint64_t can_victron_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

static bool can_victron_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
{
    if (buffer == NULL || buffer_size == 0 || offset == NULL) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *offset, buffer_size - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    size_t remaining = buffer_size - *offset;
    if ((size_t)written >= remaining) {
        return false;
    }

    *offset += (size_t)written;
    return true;
}

static void can_victron_publish_event(event_bus_event_id_t id, char *payload, size_t length)
{
    if (s_event_publisher == NULL || payload == NULL || length == 0) {
        return;
    }

    event_bus_event_t event = {
        .id = id,
        .payload = payload,
        .payload_size = length + 1,
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish CAN event %u", (unsigned)id);
    }
}

static esp_err_t can_victron_emit_events(uint32_t can_id,
                                         const uint8_t *data,
                                         size_t dlc,
                                         size_t data_length,
                                         const char *description,
                                         can_victron_direction_t direction,
                                         uint64_t timestamp)
{
    if (s_event_publisher == NULL) {
        return ESP_OK;
    }

    if (data_length > dlc) {
        data_length = dlc;
    }

    if (data_length > 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *direction_label = (direction == CAN_VICTRON_DIRECTION_RX) ? "rx" : "tx";

    size_t raw_index;
    portENTER_CRITICAL(&s_event_slot_lock);
    raw_index = s_next_event_slot;
    s_next_event_slot = (s_next_event_slot + 1U) % CAN_VICTRON_EVENT_BUFFERS;
    portEXIT_CRITICAL(&s_event_slot_lock);

    char *raw_payload = s_can_raw_events[raw_index];
    size_t raw_offset = 0;

    if (!can_victron_json_append(raw_payload,
                                 CAN_VICTRON_JSON_SIZE,
                                 &raw_offset,
                                 "{\"type\":\"can_raw\",\"direction\":\"%s\",\"timestamp\":%" PRIu64 ",\"id\":\"%08" PRIX32 "\"," \
                                 "\"dlc\":%zu,\"data\":\"",
                                 direction_label,
                                 timestamp,
                                 can_id,
                                 dlc)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < data_length; ++i) {
        if (!can_victron_json_append(raw_payload,
                                     CAN_VICTRON_JSON_SIZE,
                                     &raw_offset,
                                     "%02X",
                                     (unsigned)data[i])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!can_victron_json_append(raw_payload, CAN_VICTRON_JSON_SIZE, &raw_offset, "\"}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    can_victron_publish_event(APP_EVENT_ID_CAN_FRAME_RAW, raw_payload, raw_offset);

    size_t decoded_index;
    portENTER_CRITICAL(&s_event_slot_lock);
    decoded_index = s_next_event_slot;
    s_next_event_slot = (s_next_event_slot + 1U) % CAN_VICTRON_EVENT_BUFFERS;
    portEXIT_CRITICAL(&s_event_slot_lock);

    char *decoded_payload = s_can_decoded_events[decoded_index];
    size_t decoded_offset = 0;

    const char *label = (description != NULL) ? description : "";
    if (!can_victron_json_append(decoded_payload,
                                 CAN_VICTRON_JSON_SIZE,
                                 &decoded_offset,
                                 "{\"type\":\"can_decoded\",\"direction\":\"%s\",\"timestamp\":%" PRIu64 ",\"id\":\"%08" PRIX32 "\"," \
                                 "\"description\":\"%s\",\"bytes\":[",
                                 direction_label,
                                 timestamp,
                                 can_id,
                                 label)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < data_length; ++i) {
        if (!can_victron_json_append(decoded_payload,
                                     CAN_VICTRON_JSON_SIZE,
                                     &decoded_offset,
                                     "%s%u",
                                     (i == 0U) ? "" : ",",
                                     (unsigned)data[i])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!can_victron_json_append(decoded_payload, CAN_VICTRON_JSON_SIZE, &decoded_offset, "]}")) {
        return ESP_ERR_INVALID_SIZE;
    }

    can_victron_publish_event(APP_EVENT_ID_CAN_FRAME_DECODED, decoded_payload, decoded_offset);
    return ESP_OK;
}

static void can_victron_publish_demo_frames(void)
{
    static const uint8_t k_demo_status[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint64_t timestamp = can_victron_timestamp_ms();
    (void)can_victron_emit_events(0x18FF50E5,
                                  k_demo_status,
                                  sizeof(k_demo_status),
                                  sizeof(k_demo_status),
                                  "Battery status frame",
                                  CAN_VICTRON_DIRECTION_TX,
                                  timestamp);

    static const uint8_t k_demo_alarm[] = {0x01, 0x02, 0x00, 0x00};
    (void)can_victron_emit_events(0x18FF01E5,
                                  k_demo_alarm,
                                  sizeof(k_demo_alarm),
                                  sizeof(k_demo_alarm),
                                  "Alarm flags",
                                  CAN_VICTRON_DIRECTION_TX,
                                  can_victron_timestamp_ms());
}

#ifdef ESP_PLATFORM
static uint32_t can_victron_effective_interval_ms(const config_manager_can_settings_t *settings)
{
    uint32_t interval = (settings != NULL) ? settings->keepalive.interval_ms : 0U;
    if (interval == 0U) {
        interval = CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS;
        if (interval == 0U) {
            interval = 1000U;
        }
    }
    return interval;
}

static uint32_t can_victron_effective_retry_ms(const config_manager_can_settings_t *settings)
{
    if (settings == NULL) {
        return CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS;
    }
    return settings->keepalive.retry_ms;
}

static uint32_t can_victron_effective_timeout_ms(const config_manager_can_settings_t *settings)
{
    if (settings == NULL) {
        return CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS;
    }
    return settings->keepalive.timeout_ms;
}

static esp_err_t can_victron_start_driver(void)
{
    // Check driver state with mutex protection
    bool already_started = false;
    if (s_driver_state_mutex != NULL && xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        already_started = s_driver_started;
        xSemaphoreGive(s_driver_state_mutex);
    }

    if (already_started) {
        return ESP_OK;
    }

    const config_manager_can_settings_t *settings = can_victron_get_settings();
    int tx_gpio = CONFIG_TINYBMS_CAN_VICTRON_TX_GPIO;
    int rx_gpio = CONFIG_TINYBMS_CAN_VICTRON_RX_GPIO;
    if (settings != NULL) {
        if (settings->twai.tx_gpio >= 0) {
            tx_gpio = settings->twai.tx_gpio;
        }
        if (settings->twai.rx_gpio >= 0) {
            rx_gpio = settings->twai.rx_gpio;
        }
    }

    s_twai_tx_gpio = tx_gpio;
    s_twai_rx_gpio = rx_gpio;

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)s_twai_tx_gpio,
                                    (gpio_num_t)s_twai_rx_gpio,
                                    TWAI_MODE_NORMAL);
    g_config.tx_queue_len = CAN_VICTRON_TWAI_TX_QUEUE_LEN;
    g_config.rx_queue_len = CAN_VICTRON_TWAI_RX_QUEUE_LEN;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = {
        .acceptance_code = (uint32_t)(CAN_VICTRON_KEEPALIVE_ID << 21),
        .acceptance_mask = ~(0x7FFU << 21),
        .single_filter = true,
    };

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        (void)twai_driver_uninstall();
        return err;
    }

    // Set driver state with mutex protection
    if (s_driver_state_mutex != NULL && xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_driver_started = true;
        xSemaphoreGive(s_driver_state_mutex);
    }

    s_keepalive_ok = false;
    uint64_t now = can_victron_timestamp_ms();
    s_last_keepalive_rx_ms = now;
    uint32_t interval = can_victron_effective_interval_ms(settings);
    if (now >= interval) {
        s_last_keepalive_tx_ms = now - interval;
    } else {
        s_last_keepalive_tx_ms = 0;
    }
    return ESP_OK;
}

static void can_victron_stop_driver(void)
{
    // Check and update driver state with mutex protection
    bool should_stop = false;
    if (s_driver_state_mutex != NULL && xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        should_stop = s_driver_started;
        if (should_stop) {
            s_driver_started = false;
        }
        xSemaphoreGive(s_driver_state_mutex);
    }

    if (!should_stop) {
        return;
    }

    (void)twai_stop();
    (void)twai_driver_uninstall();
}

static bool can_victron_is_driver_started(void)
{
    bool started = false;
    if (s_driver_state_mutex != NULL && xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        started = s_driver_started;
        xSemaphoreGive(s_driver_state_mutex);
    }
    return started;
}

static void can_victron_send_keepalive(uint64_t now)
{
    if (!can_victron_is_driver_started()) {
        return;
    }

    uint8_t payload[CAN_VICTRON_KEEPALIVE_DLC] = {0x00};
    esp_err_t err = can_victron_publish_frame(CAN_VICTRON_KEEPALIVE_ID,
                                              payload,
                                              CAN_VICTRON_KEEPALIVE_DLC,
                                              "Victron keepalive");
    if (err == ESP_OK) {
        s_last_keepalive_tx_ms = now;
    } else {
        ESP_LOGW(TAG, "Failed to transmit keepalive: %s", esp_err_to_name(err));
    }
}

static void can_victron_process_keepalive_rx(bool remote_request, uint64_t now)
{
    s_last_keepalive_rx_ms = now;
    if (!s_keepalive_ok) {
        s_keepalive_ok = true;
        ESP_LOGI(TAG, "Victron keepalive detected");
    }

    if (remote_request) {
        ESP_LOGD(TAG, "Victron keepalive request received");
        can_victron_send_keepalive(now);
    }
}

static void can_victron_service_keepalive(uint64_t now)
{
    if (!can_victron_is_driver_started()) {
        return;
    }

    const config_manager_can_settings_t *settings = can_victron_get_settings();
    uint32_t interval = can_victron_effective_interval_ms(settings);
    uint32_t retry = can_victron_effective_retry_ms(settings);
    uint32_t timeout = can_victron_effective_timeout_ms(settings);

    if (!s_keepalive_ok && retry > 0U && retry < interval) {
        interval = retry;
    }

    if ((now - s_last_keepalive_tx_ms) >= interval) {
        can_victron_send_keepalive(now);
    }

    if (s_keepalive_ok && timeout > 0U && (now - s_last_keepalive_rx_ms) > timeout) {
        s_keepalive_ok = false;
        ESP_LOGW(TAG,
                 "Victron keepalive timeout after %" PRIu64 " ms",
                 now - s_last_keepalive_rx_ms);
        can_victron_send_keepalive(now);
    }
}

static void can_victron_handle_rx_message(const twai_message_t *message)
{
    if (message == NULL) {
        return;
    }

    const bool is_remote = (message->flags & TWAI_MSG_FLAG_RTR) != 0U;
    const bool is_extended = (message->flags & TWAI_MSG_FLAG_EXTD) != 0U;
    const uint32_t identifier = message->identifier;
    const size_t dlc = message->data_length_code;
    const size_t data_length = is_remote ? 0U : dlc;
    const uint8_t *payload = is_remote ? NULL : message->data;
    uint64_t timestamp = can_victron_timestamp_ms();

    if (!is_extended && identifier == CAN_VICTRON_KEEPALIVE_ID) {
        can_victron_process_keepalive_rx(is_remote, timestamp);

        const char *desc = is_remote ? "Victron keepalive request" : "Victron keepalive";
        (void)can_victron_emit_events(identifier,
                                      payload,
                                      dlc,
                                      data_length,
                                      desc,
                                      CAN_VICTRON_DIRECTION_RX,
                                      timestamp);
    }
}

static void can_victron_task(void *context)
{
    (void)context;
    while (true) {
        uint64_t now = can_victron_timestamp_ms();

        if (can_victron_is_driver_started()) {
            twai_message_t message = {0};
            while (true) {
                esp_err_t rx = twai_receive(&message, pdMS_TO_TICKS(CAN_VICTRON_RX_TIMEOUT_MS));
                if (rx == ESP_OK) {
                    can_victron_handle_rx_message(&message);
                } else if (rx == ESP_ERR_TIMEOUT) {
                    break;
                } else {
                    ESP_LOGW(TAG, "CAN receive error: %s", esp_err_to_name(rx));
                    break;
                }
            }

            can_victron_service_keepalive(now);
        }

        vTaskDelay(pdMS_TO_TICKS(CAN_VICTRON_TASK_DELAY_MS));
    }
}
#endif  // ESP_PLATFORM

void can_victron_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

esp_err_t can_victron_publish_frame(uint32_t can_id,
                                    const uint8_t *data,
                                    size_t length,
                                    const char *description)
{
    if (length > 8U) {
        length = 8U;
    }

    size_t dlc = length;
    size_t data_length = length;

    if (data_length > 0U && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef ESP_PLATFORM
    if (!can_victron_is_driver_started()) {
        return ESP_ERR_INVALID_STATE;
    }

    twai_message_t message = {
        .identifier = can_id,
        .flags = 0,
        .data_length_code = (uint8_t)dlc,
    };

    if (data_length > 0U) {
        memcpy(message.data, data, data_length);
    }

    if (can_id > 0x7FFU) {
        message.flags |= TWAI_MSG_FLAG_EXTD;
    }

    SemaphoreHandle_t mutex = s_twai_mutex;
    if (mutex != NULL) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(CAN_VICTRON_LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Timed out acquiring CAN TX mutex");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t tx_err = twai_transmit(&message, pdMS_TO_TICKS(CAN_VICTRON_TX_TIMEOUT_MS));

    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }

    if (tx_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to transmit CAN frame 0x%08" PRIX32 ": %s",
                 can_id,
                 esp_err_to_name(tx_err));
        return tx_err;
    }
#endif

    uint64_t timestamp = can_victron_timestamp_ms();
    return can_victron_emit_events(can_id,
                                   data,
                                   dlc,
                                   data_length,
                                   description,
                                   CAN_VICTRON_DIRECTION_TX,
                                   timestamp);
}

void can_victron_init(void)
{
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Initialising Victron CAN interface");

    // Create mutexes before starting driver
    if (s_twai_mutex == NULL) {
        s_twai_mutex = xSemaphoreCreateMutex();
        if (s_twai_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create CAN mutex");
        }
    }

    if (s_driver_state_mutex == NULL) {
        s_driver_state_mutex = xSemaphoreCreateMutex();
        if (s_driver_state_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create driver state mutex");
            return;
        }
    }

    esp_err_t err = can_victron_start_driver();
    if (err == ESP_OK) {
        if (s_can_task_handle == NULL) {
            BaseType_t rc = xTaskCreate(can_victron_task,
                                        "can_victron",
                                        CAN_VICTRON_TASK_STACK,
                                        NULL,
                                        CAN_VICTRON_TASK_PRIORITY,
                                        &s_can_task_handle);
            if (rc != pdPASS) {
                ESP_LOGE(TAG, "Failed to create Victron CAN task");
                s_can_task_handle = NULL;
                can_victron_stop_driver();
            }
        }

        if (can_victron_is_driver_started()) {
            uint64_t now = can_victron_timestamp_ms();
            can_victron_send_keepalive(now);
            ESP_LOGI(TAG,
                     "Victron CAN driver ready (TX=%d RX=%d)",
                     s_twai_tx_gpio,
                     s_twai_rx_gpio);
        }
    } else {
        ESP_LOGE(TAG, "Victron CAN driver start failed: %s", esp_err_to_name(err));
    }

    if (!can_victron_is_driver_started()) {
        can_victron_publish_demo_frames();
    }
#else
    ESP_LOGI(TAG, "Victron CAN monitor initialised (host mode)");
    can_victron_publish_demo_frames();
#endif
}

