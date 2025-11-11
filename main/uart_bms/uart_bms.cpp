#include "uart_bms.h"

#include <cinttypes>
#include <cstdarg>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <sys/time.h>
#endif

#include "driver/gpio.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_events.h"
#include "conversion_table.h"
#include "uart_frame_builder.h"
#include "uart_response_parser.h"

#ifndef CONFIG_TINYBMS_UART_TX_GPIO
#define CONFIG_TINYBMS_UART_TX_GPIO 37
#endif

#ifndef CONFIG_TINYBMS_UART_RX_GPIO
#define CONFIG_TINYBMS_UART_RX_GPIO 36
#endif

#define UART_BMS_UART_PORT       UART_NUM_1
#define UART_BMS_BAUD_RATE       115200
#define UART_BMS_RX_BUFFER_SIZE  256
#define UART_BMS_TASK_STACK      4096
#define UART_BMS_TASK_PRIORITY   12
#define UART_BMS_MAX_FRAME_SIZE  128
#define UART_BMS_LISTENER_SLOTS  4
#define UART_BMS_EVENT_BUFFERS   4

#define UART_BMS_SYSTEM_CONTROL_REGISTER      0x0086U
#define UART_BMS_SYSTEM_CONTROL_RESTART_VALUE 0xA55AU
namespace {

constexpr char kTag[] = "uart_bms";
constexpr gpio_num_t kUartTxPin = static_cast<gpio_num_t>(CONFIG_TINYBMS_UART_TX_GPIO);
constexpr gpio_num_t kUartRxPin = static_cast<gpio_num_t>(CONFIG_TINYBMS_UART_RX_GPIO);

struct ListenerEntry {
    uart_bms_data_callback_t callback = nullptr;
    void* context = nullptr;
};

struct SharedListenerEntry {
    uart_bms_shared_callback_t callback = nullptr;
    void* context = nullptr;
};

uint8_t s_poll_request[UART_BMS_MAX_FRAME_SIZE] = {0};
size_t s_poll_request_length = 0;

event_bus_publish_fn_t s_event_publisher = nullptr;
ListenerEntry s_listeners[UART_BMS_LISTENER_SLOTS] = {};
SharedListenerEntry s_shared_listeners[UART_BMS_LISTENER_SLOTS] = {};
uart_bms_live_data_t s_event_buffers[UART_BMS_EVENT_BUFFERS];
size_t s_next_event_buffer = 0;
char s_uart_raw_json[UART_BMS_EVENT_BUFFERS][UART_BMS_FRAME_JSON_SIZE];
char s_uart_decoded_json[UART_BMS_EVENT_BUFFERS][UART_BMS_FRAME_JSON_SIZE];
size_t s_next_uart_json_buffer = 0;
bool s_uart_initialised = false;
TaskHandle_t s_uart_poll_task_handle = nullptr;
uint8_t s_rx_buffer[UART_BMS_MAX_FRAME_SIZE] = {0};
size_t s_rx_length = 0;
#ifdef ESP_PLATFORM
portMUX_TYPE s_poll_interval_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
uint32_t s_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
SemaphoreHandle_t s_command_mutex = nullptr;
SemaphoreHandle_t s_rx_buffer_mutex = nullptr;
SemaphoreHandle_t s_snapshot_mutex = nullptr;
SemaphoreHandle_t s_listeners_mutex = nullptr;
SemaphoreHandle_t s_shared_listeners_mutex = nullptr;  // Protection for s_shared_listeners

// Flag pour pause de polling (évite deadlock vTaskSuspend)
static volatile bool s_poll_pause_requested = false;

// Flag pour arrêt propre de la task
static volatile bool s_task_should_exit = false;

// Spinlock pour protection event buffer
static portMUX_TYPE s_event_buffer_lock = portMUX_INITIALIZER_UNLOCKED;

TinyBMS_LiveData s_shared_snapshot{};
bool s_shared_snapshot_valid = false;
UartResponseParser s_response_parser;

esp_err_t uart_bms_prepare_poll_request()
{
    if (s_poll_request_length != 0) {
        return ESP_OK;
    }

    return uart_frame_builder_build_poll_request(s_poll_request,
                                                 sizeof(s_poll_request),
                                                 &s_poll_request_length);
}

static uint32_t uart_bms_clamp_poll_interval(uint32_t interval_ms)
{
    if (interval_ms < UART_BMS_MIN_POLL_INTERVAL_MS) {
        return UART_BMS_MIN_POLL_INTERVAL_MS;
    }
    if (interval_ms > UART_BMS_MAX_POLL_INTERVAL_MS) {
        return UART_BMS_MAX_POLL_INTERVAL_MS;
    }
    return interval_ms;
}

static uint64_t uart_bms_timestamp_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

static void uart_bms_notify_listeners(const uart_bms_live_data_t *data)
{
    can_publisher_conversion_ingest_sample(data);

    // Copier les callbacks dans un buffer local sous mutex
    ListenerEntry local_listeners[UART_BMS_LISTENER_SLOTS];

    if (s_listeners_mutex != nullptr && xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(local_listeners, s_listeners, sizeof(local_listeners));
        xSemaphoreGive(s_listeners_mutex);
    } else {
        // Si mutex non disponible, skip notification pour éviter race condition
        return;
    }

    // Invoquer callbacks en dehors du mutex
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (local_listeners[i].callback != nullptr) {
            local_listeners[i].callback(data, local_listeners[i].context);
        }
    }
}

static void uart_bms_notify_shared_listeners(const TinyBMS_LiveData& data)
{
    // Copier les callbacks dans un buffer local sous mutex (évite race condition)
    SharedListenerEntry local_listeners[UART_BMS_LISTENER_SLOTS];

    if (s_shared_listeners_mutex != nullptr &&
        xSemaphoreTake(s_shared_listeners_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(local_listeners, s_shared_listeners, sizeof(local_listeners));
        xSemaphoreGive(s_shared_listeners_mutex);
    } else {
        // Si mutex non disponible, skip notification pour éviter race condition
        return;
    }

    // Invoquer callbacks en dehors du mutex
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (local_listeners[i].callback != nullptr) {
            local_listeners[i].callback(data, local_listeners[i].context);
        }
    }
}

static void uart_bms_publish_live_data(const uart_bms_live_data_t *data)
{
    if (s_event_publisher != nullptr) {
        // Protéger l'accès à l'index du buffer avec spinlock
        portENTER_CRITICAL(&s_event_buffer_lock);
        uart_bms_live_data_t* storage = &s_event_buffers[s_next_event_buffer];
        s_next_event_buffer = (s_next_event_buffer + 1U) % UART_BMS_EVENT_BUFFERS;
        portEXIT_CRITICAL(&s_event_buffer_lock);

        *storage = *data;

        event_bus_event_t event{};
        event.id = APP_EVENT_ID_BMS_LIVE_DATA;
        event.payload = storage;
        event.payload_size = sizeof(*storage);

        if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
            ESP_LOGW(kTag, "Unable to publish TinyBMS live data event");
        }
    }

    uart_bms_notify_listeners(data);
}

static bool uart_bms_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
{
    if (buffer == nullptr || buffer_size == 0 || offset == nullptr) {
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

static void uart_bms_publish_frame_events(const uint8_t *frame,
                                          size_t length,
                                          const uart_bms_live_data_t *decoded)
{
    if (s_event_publisher == nullptr || frame == nullptr || decoded == nullptr) {
        return;
    }

    size_t raw_index = s_next_uart_json_buffer;
    s_next_uart_json_buffer = (s_next_uart_json_buffer + 1) % UART_BMS_EVENT_BUFFERS;

    char *raw_json = s_uart_raw_json[raw_index];
    size_t raw_offset = 0;
    if (uart_bms_json_append(raw_json,
                             UART_BMS_FRAME_JSON_SIZE,
                             &raw_offset,
                             "{\"type\":\"uart_raw\",\"timestamp_ms\":%" PRIu64 ",\"timestamp\":%" PRIu64
                             ",\"length\":%zu,\"data\":\"",
                             decoded->timestamp_ms,
                             decoded->timestamp_ms,
                             length)) {
        for (size_t i = 0; i < length; ++i) {
            if (!uart_bms_json_append(raw_json,
                                      UART_BMS_FRAME_JSON_SIZE,
                                      &raw_offset,
                                      "%02X",
                                      (unsigned)frame[i])) {
                ESP_LOGW(kTag, "UART raw frame JSON truncated");
                raw_offset = 0;
                break;
            }
        }

        if (raw_offset > 0 &&
            uart_bms_json_append(raw_json, UART_BMS_FRAME_JSON_SIZE, &raw_offset, "\"}")) {
            event_bus_event_t raw_event{};
            raw_event.id = APP_EVENT_ID_UART_FRAME_RAW;
            raw_event.payload = raw_json;
            raw_event.payload_size = raw_offset + 1;
            if (!s_event_publisher(&raw_event, pdMS_TO_TICKS(50))) {
                ESP_LOGW(kTag, "Unable to publish UART raw frame event");
            }
        }
    }

    size_t decoded_index = s_next_uart_json_buffer;
    s_next_uart_json_buffer = (s_next_uart_json_buffer + 1) % UART_BMS_EVENT_BUFFERS;

    char *decoded_json = s_uart_decoded_json[decoded_index];
    size_t decoded_offset = 0;
    if (!uart_bms_json_append(decoded_json,
                              UART_BMS_FRAME_JSON_SIZE,
                              &decoded_offset,
                              "{\"type\":\"uart_decoded\",\"timestamp_ms\":%" PRIu64 ",\"timestamp\":%" PRIu64
                              ",\"pack_voltage\":%.3f,\"pack_current\":%.3f,\"state_of_charge\":%.2f,\"state_of_health\":%.2f,"
                              "\"average_temperature\":%.2f,\"mos_temperature\":%.2f,\"uptime_seconds\":%" PRIu32 ","
                              "\"cycle_count\":%" PRIu32 ",\"registers\":[",
                              decoded->timestamp_ms,
                              decoded->timestamp_ms,
                              decoded->pack_voltage_v,
                              decoded->pack_current_a,
                              decoded->state_of_charge_pct,
                              decoded->state_of_health_pct,
                              decoded->average_temperature_c,
                              decoded->mosfet_temperature_c,
                              decoded->uptime_seconds,
                              decoded->cycle_count)) {
        return;
    }

    for (size_t i = 0; i < decoded->register_count; ++i) {
        const uart_bms_register_entry_t *entry = &decoded->registers[i];
        if (!uart_bms_json_append(decoded_json,
                                  UART_BMS_FRAME_JSON_SIZE,
                                  &decoded_offset,
                                  "%s{\"address\":%u,\"value\":%u}",
                                  (i == 0) ? "" : ",",
                                  (unsigned)entry->address,
                                  (unsigned)entry->raw_value)) {
            ESP_LOGW(kTag, "UART decoded frame JSON truncated");
            return;
        }
    }

    if (!uart_bms_json_append(decoded_json,
                              UART_BMS_FRAME_JSON_SIZE,
                              &decoded_offset,
                              "],\"alarm_bits\":%u,\"warning_bits\":%u,\"balancing_bits\":%u}",
                              (unsigned)decoded->alarm_bits,
                              (unsigned)decoded->warning_bits,
                              (unsigned)decoded->balancing_bits)) {
        ESP_LOGW(kTag, "UART decoded frame JSON truncated");
        return;
    }

    event_bus_event_t decoded_event{};
    decoded_event.id = APP_EVENT_ID_UART_FRAME_DECODED;
    decoded_event.payload = decoded_json;
    decoded_event.payload_size = decoded_offset + 1;

    if (!s_event_publisher(&decoded_event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(kTag, "Unable to publish UART decoded frame event");
    }
}

static void uart_bms_reset_buffer(void)
{
#ifdef ESP_PLATFORM
    if (s_rx_buffer_mutex != nullptr) {
        xSemaphoreTake(s_rx_buffer_mutex, portMAX_DELAY);
    }
#endif
    s_rx_length = 0;
#ifdef ESP_PLATFORM
    if (s_rx_buffer_mutex != nullptr) {
        xSemaphoreGive(s_rx_buffer_mutex);
    }
#endif
}

#ifdef ESP_PLATFORM
static esp_err_t uart_bms_read_frame_blocking(uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t timeout_ms,
                                              size_t* out_length)
{
    if (buffer == nullptr || buffer_size < 5) {
        return ESP_ERR_INVALID_ARG;
    }

    if (timeout_ms == 0U) {
        timeout_ms = UART_BMS_RESPONSE_TIMEOUT_MS;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    size_t offset = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            return ESP_ERR_TIMEOUT;
        }

        int bytes_read = uart_read_bytes(UART_BMS_UART_PORT,
                                         buffer + offset,
                                         1,
                                         pdMS_TO_TICKS(20));
        if (bytes_read < 0) {
            return ESP_FAIL;
        }
        if (bytes_read == 0) {
            continue;
        }

        offset += (size_t)bytes_read;

        while (offset > 0 && buffer[0] != 0xAA) {
            memmove(buffer, buffer + 1, offset - 1);
            --offset;
        }

        if (offset >= 3) {
            size_t payload_len = buffer[2];
            size_t total_len = payload_len + 5;
            if (total_len > buffer_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (offset >= total_len) {
                uint16_t crc_expected = static_cast<uint16_t>(buffer[total_len - 2]) |
                                        static_cast<uint16_t>(buffer[total_len - 1] << 8);
                uint16_t crc_computed = uart_frame_builder_crc16(buffer, total_len - 2);
                if (crc_expected != crc_computed) {
                    return ESP_ERR_INVALID_CRC;
                }
                if (out_length != nullptr) {
                    *out_length = total_len;
                }
                return ESP_OK;
            }
        }
    }
}

static esp_err_t uart_bms_wait_for_ack(uint32_t timeout_ms)
{
    uint8_t frame[UART_BMS_MAX_FRAME_SIZE] = {0};
    size_t frame_len = 0;
    esp_err_t err = uart_bms_read_frame_blocking(frame, sizeof(frame), timeout_ms, &frame_len);
    if (err != ESP_OK) {
        return err;
    }

    if (frame_len < 5 || frame[1] == 0x09) {
        return ESP_ERR_INVALID_STATE;
    }

    if (frame[1] == 0x01U) {
        return ESP_OK;
    }

    if (frame[1] == 0x81U) {
        uint8_t error_code = (frame_len > 3) ? frame[3] : 0U;
        ESP_LOGW(kTag, "TinyBMS negative ACK (0x%02X)", (unsigned)error_code);
        return ESP_FAIL;
    }

    ESP_LOGW(kTag, "Unexpected TinyBMS opcode 0x%02X while awaiting ACK", (unsigned)frame[1]);
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t uart_bms_read_register_blocking(uint16_t address,
                                                 uint32_t timeout_ms,
                                                 uint16_t* out_value)
{
    uint8_t request[UART_BMS_MAX_FRAME_SIZE] = {0};
    size_t request_len = 0;
    esp_err_t err = uart_frame_builder_build_read_register(request,
                                                           sizeof(request),
                                                           address,
                                                           &request_len);
    if (err != ESP_OK) {
        return err;
    }

    int written = uart_write_bytes(UART_BMS_UART_PORT, reinterpret_cast<const char*>(request), request_len);
    if (written < 0 || (size_t)written != request_len) {
        ESP_LOGW(kTag, "Failed to send read request for 0x%04X", (unsigned)address);
        return ESP_FAIL;
    }

    uint8_t response[UART_BMS_MAX_FRAME_SIZE] = {0};
    size_t response_len = 0;
    err = uart_bms_read_frame_blocking(response, sizeof(response), timeout_ms, &response_len);
    if (err != ESP_OK) {
        return err;
    }

    if (response_len < 5 || response[1] != 0x07U || response[2] < 2U) {
        ESP_LOGW(kTag, "Invalid read response opcode 0x%02X", (unsigned)response[1]);
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t raw = static_cast<uint16_t>(response[3]) |
                   static_cast<uint16_t>(response[4] << 8);
    if (out_value != nullptr) {
        *out_value = raw;
    }
    return ESP_OK;
}
#endif  // ESP_PLATFORM

static void uart_bms_consume_bytes(const uint8_t *data, size_t length)
{
#ifdef ESP_PLATFORM
    if (s_rx_buffer_mutex != nullptr) {
        xSemaphoreTake(s_rx_buffer_mutex, portMAX_DELAY);
    }
#endif

    for (size_t i = 0; i < length; ++i) {
        if (s_rx_length >= sizeof(s_rx_buffer)) {
            ESP_LOGW(kTag, "RX buffer overflow, resetting synchronisation");
#ifdef ESP_PLATFORM
            if (s_rx_buffer_mutex != nullptr) {
                xSemaphoreGive(s_rx_buffer_mutex);
            }
#endif
            uart_bms_reset_buffer();
#ifdef ESP_PLATFORM
            if (s_rx_buffer_mutex != nullptr) {
                xSemaphoreTake(s_rx_buffer_mutex, portMAX_DELAY);
            }
#endif
        }

        s_rx_buffer[s_rx_length++] = data[i];

        bool progress = true;
        while (progress) {
            progress = false;

            if (s_rx_length < 3) {
                break;
            }

            if (s_rx_buffer[0] != 0xAA) {
                std::memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            size_t payload_len = s_rx_buffer[2];
            size_t total_len = payload_len + 5;
            if (total_len > UART_BMS_MAX_FRAME_SIZE) {
                ESP_LOGW(kTag, "Frame length %zu exceeds buffer, dropping byte", total_len);
                std::memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            if (s_rx_length < total_len) {
                break;
            }

            esp_err_t err = uart_bms_process_frame(s_rx_buffer, total_len);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Failed to process TinyBMS frame: %s", esp_err_to_name(err));
                std::memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            if (s_rx_length > total_len) {
                std::memmove(s_rx_buffer, s_rx_buffer + total_len, s_rx_length - total_len);
            }
            s_rx_length -= total_len;
            progress = (s_rx_length > 0);
        }
    }

#ifdef ESP_PLATFORM
    if (s_rx_buffer_mutex != nullptr) {
        xSemaphoreGive(s_rx_buffer_mutex);
    }
#endif
}

/**
 * @brief Send UART command with automatic retry for sleep mode wake-up
 *
 * Implements the sleep mode handling as specified in TinyBMS documentation:
 * "If Tiny BMS device is in sleep mode, the first command must be send twice.
 * After received the first command BMS wakes up from sleep mode, but the
 * response to the command will be sent when it receives the command a second time."
 *
 * @param frame Command frame to send
 * @param frame_length Length of the frame
 * @param read_buffer Buffer to store received bytes
 * @param read_buffer_size Size of read buffer
 * @param timeout_ms Timeout for waiting response
 * @param received_any_bytes Output: true if any bytes were received
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no response after retry
 */
static esp_err_t uart_bms_send_with_wakeup(const uint8_t* frame,
                                            size_t frame_length,
                                            uint8_t* read_buffer,
                                            size_t read_buffer_size,
                                            uint32_t timeout_ms,
                                            bool* received_any_bytes)
{
    if (frame == nullptr || read_buffer == nullptr || received_any_bytes == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *received_any_bytes = false;

    // First send (may wake up BMS from sleep)
    int written = uart_write_bytes(UART_BMS_UART_PORT,
                                    reinterpret_cast<const char*>(frame),
                                    frame_length);
    if (written < 0 || static_cast<size_t>(written) != frame_length) {
        ESP_LOGW(kTag, "Failed to send command (wrote %d of %zu bytes)", written, frame_length);
        return ESP_ERR_INVALID_STATE;
    }

    // Try to receive response with timeout
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    bool got_response = false;

    while (xTaskGetTickCount() < deadline) {
        int bytes_read = uart_read_bytes(UART_BMS_UART_PORT,
                                         read_buffer,
                                         read_buffer_size,
                                         pdMS_TO_TICKS(20));
        if (bytes_read > 0) {
            uart_bms_consume_bytes(read_buffer, static_cast<size_t>(bytes_read));
            got_response = true;
            *received_any_bytes = true;
        } else if (bytes_read < 0) {
            ESP_LOGW(kTag, "UART read error: %d", bytes_read);
            break;
        }
    }

    // If no response, BMS might have been asleep - retry
    if (!got_response) {
        ESP_LOGD(kTag, "No response on first attempt, retrying (BMS may have been in sleep mode)");

        // Flush any pending data
        uart_flush_input(UART_BMS_UART_PORT);

        // Wait a bit for BMS to fully wake up
        vTaskDelay(pdMS_TO_TICKS(50));

        // Second send (BMS should be awake now)
        written = uart_write_bytes(UART_BMS_UART_PORT,
                                   reinterpret_cast<const char*>(frame),
                                   frame_length);
        if (written < 0 || static_cast<size_t>(written) != frame_length) {
            ESP_LOGW(kTag, "Failed to send command on retry");
            return ESP_ERR_INVALID_STATE;
        }

        // Wait for response again
        deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
        while (xTaskGetTickCount() < deadline) {
            int bytes_read = uart_read_bytes(UART_BMS_UART_PORT,
                                             read_buffer,
                                             read_buffer_size,
                                             pdMS_TO_TICKS(20));
            if (bytes_read > 0) {
                uart_bms_consume_bytes(read_buffer, static_cast<size_t>(bytes_read));
                got_response = true;
                *received_any_bytes = true;
            } else if (bytes_read < 0) {
                ESP_LOGW(kTag, "UART read error on retry: %d", bytes_read);
                break;
            }
        }
    }

    if (!got_response) {
        ESP_LOGW(kTag, "No response after wake-up retry");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void uart_poll_task(void *arg)
{
    (void)arg;
    uint8_t read_buffer[64];

    TickType_t last_wake_time = xTaskGetTickCount();

    while (!s_task_should_exit) {
        // Vérifier si une pause est demandée (évite deadlock avec vTaskSuspend)
        while (s_poll_pause_requested && !s_task_should_exit) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Sortir si demandé
        if (s_task_should_exit) {
            break;
        }

        esp_err_t frame_err = uart_bms_prepare_poll_request();
        if (frame_err != ESP_OK || s_poll_request_length == 0) {
            ESP_LOGE(kTag,
                     "Unable to prepare TinyBMS poll request: %s",
                     esp_err_to_name(frame_err));
            vTaskDelay(pdMS_TO_TICKS(UART_BMS_MIN_POLL_INTERVAL_MS));
            continue;
        }

        // Use wake-up aware send for sleep mode handling
        bool received_bytes = false;
        esp_err_t send_err = uart_bms_send_with_wakeup(s_poll_request,
                                                        s_poll_request_length,
                                                        read_buffer,
                                                        sizeof(read_buffer),
                                                        UART_BMS_RESPONSE_TIMEOUT_MS,
                                                        &received_bytes);

        if (!received_bytes) {
            ESP_LOGW(kTag, "TinyBMS poll timed out (no response)");
            s_response_parser.recordTimeout();
        }

        uint32_t interval_ms = uart_bms_get_poll_interval_ms();
        TickType_t interval_ticks = pdMS_TO_TICKS(interval_ms);
        if (interval_ticks == 0) {
            interval_ticks = 1;
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }

    ESP_LOGI(kTag, "UART BMS poll task exiting");
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" {

void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void uart_bms_set_poll_interval_ms(uint32_t interval_ms)
{
    uint32_t clamped = uart_bms_clamp_poll_interval(interval_ms);
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_poll_interval_lock);
#endif
    bool changed = (s_poll_interval_ms != clamped);
    s_poll_interval_ms = clamped;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&s_poll_interval_lock);
#endif
    if (changed) {
        ESP_LOGI(kTag, "TinyBMS poll interval set to %u ms", (unsigned)clamped);
    }
}

uint32_t uart_bms_get_poll_interval_ms(void)
{
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_poll_interval_lock);
#endif
    uint32_t interval = s_poll_interval_ms;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&s_poll_interval_lock);
#endif
    return interval;
}

void uart_bms_init(void)
{
    if (s_uart_initialised) {
        return;
    }

    uart_config_t config = {
        .baud_rate = UART_BMS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config(UART_BMS_UART_PORT, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to configure UART: %s", esp_err_to_name(err));
        return;
    }

    err = uart_set_pin(UART_BMS_UART_PORT,
                       kUartTxPin,
                       kUartRxPin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to set UART pins: %s", esp_err_to_name(err));
        return;
    }

    err = uart_driver_install(UART_BMS_UART_PORT,
                              UART_BMS_RX_BUFFER_SIZE,
                              UART_BMS_RX_BUFFER_SIZE,
                              0,
                              nullptr,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to install UART driver: %s", esp_err_to_name(err));
        return;
    }

    esp_err_t frame_err = uart_bms_prepare_poll_request();
    if (frame_err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to initialise TinyBMS poll frame: %s", esp_err_to_name(frame_err));
        uart_driver_delete(UART_BMS_UART_PORT);
        return;
    }

    if (s_command_mutex == nullptr) {
        s_command_mutex = xSemaphoreCreateMutex();
        if (s_command_mutex == nullptr) {
            ESP_LOGE(kTag, "Unable to allocate TinyBMS command mutex");
            uart_driver_delete(UART_BMS_UART_PORT);
            return;
        }
    }

    if (s_rx_buffer_mutex == nullptr) {
        s_rx_buffer_mutex = xSemaphoreCreateMutex();
        if (s_rx_buffer_mutex == nullptr) {
            ESP_LOGE(kTag, "Unable to allocate TinyBMS RX buffer mutex");
            uart_driver_delete(UART_BMS_UART_PORT);
            return;
        }
    }

    if (s_snapshot_mutex == nullptr) {
        s_snapshot_mutex = xSemaphoreCreateMutex();
        if (s_snapshot_mutex == nullptr) {
            ESP_LOGE(kTag, "Unable to allocate TinyBMS snapshot mutex");
            uart_driver_delete(UART_BMS_UART_PORT);
            return;
        }
    }

    if (s_listeners_mutex == nullptr) {
        s_listeners_mutex = xSemaphoreCreateMutex();
        if (s_listeners_mutex == nullptr) {
            ESP_LOGE(kTag, "Unable to allocate TinyBMS listeners mutex");
            uart_driver_delete(UART_BMS_UART_PORT);
            return;
        }
    }

    if (s_shared_listeners_mutex == nullptr) {
        s_shared_listeners_mutex = xSemaphoreCreateMutex();
        if (s_shared_listeners_mutex == nullptr) {
            ESP_LOGE(kTag, "Unable to allocate TinyBMS shared listeners mutex");
            uart_driver_delete(UART_BMS_UART_PORT);
            return;
        }
    }

    s_uart_initialised = true;

    esp_err_t energy_err = can_publisher_conversion_restore_energy_state();
    if (energy_err == ESP_OK) {
        ESP_LOGI(kTag, "Energy counters restored from NVS");
    } else if (energy_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(kTag, "Failed to restore energy counters: %s", esp_err_to_name(energy_err));
    }

    if (xTaskCreate(uart_poll_task,
                    "uart_poll",
                    UART_BMS_TASK_STACK,
                    nullptr,
                    UART_BMS_TASK_PRIORITY,
                    &s_uart_poll_task_handle) != pdPASS) {
        ESP_LOGE(kTag, "Unable to create UART BMS task");

        // Nettoyer tous les mutex créés
        if (s_command_mutex != nullptr) {
            vSemaphoreDelete(s_command_mutex);
            s_command_mutex = nullptr;
        }
        if (s_rx_buffer_mutex != nullptr) {
            vSemaphoreDelete(s_rx_buffer_mutex);
            s_rx_buffer_mutex = nullptr;
        }
        if (s_snapshot_mutex != nullptr) {
            vSemaphoreDelete(s_snapshot_mutex);
            s_snapshot_mutex = nullptr;
        }
        if (s_listeners_mutex != nullptr) {
            vSemaphoreDelete(s_listeners_mutex);
            s_listeners_mutex = nullptr;
        }
        if (s_shared_listeners_mutex != nullptr) {
            vSemaphoreDelete(s_shared_listeners_mutex);
            s_shared_listeners_mutex = nullptr;
        }

        uart_driver_delete(UART_BMS_UART_PORT);
        s_uart_initialised = false;
        s_uart_poll_task_handle = nullptr;
    }
}

esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_listeners_mutex == nullptr || xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_ERR_NO_MEM;

    // Vérifier si déjà enregistré
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            result = ESP_OK;
            goto cleanup;
        }
    }

    // Trouver slot libre
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == nullptr) {
            s_listeners[i].callback = callback;
            s_listeners[i].context = context;
            result = ESP_OK;
            goto cleanup;
        }
    }

cleanup:
    xSemaphoreGive(s_listeners_mutex);
    return result;
}

void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return;
    }

    if (s_listeners_mutex == nullptr || xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(kTag, "Failed to acquire listeners mutex for unregister");
        return;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            s_listeners[i].callback = nullptr;
            s_listeners[i].context = nullptr;
        }
    }

    xSemaphoreGive(s_listeners_mutex);
}

esp_err_t uart_bms_decode_frame(const uint8_t *frame, size_t length, uart_bms_live_data_t *out_data)
{
    if (frame == nullptr || out_data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return s_response_parser.parseFrame(frame,
                                        length,
                                        uart_bms_timestamp_ms(),
                                        out_data,
                                        nullptr);
}

esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length)
{
    if (frame == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_bms_live_data_t legacy_data{};
    TinyBMS_LiveData shared{};
    esp_err_t err = s_response_parser.parseFrame(frame,
                                                 length,
                                                 uart_bms_timestamp_ms(),
                                                 &legacy_data,
                                                 &shared);
    if (err != ESP_OK) {
        return err;
    }

#ifdef ESP_PLATFORM
    if (s_snapshot_mutex != nullptr) {
        xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    }
#endif
    s_shared_snapshot = shared;
    s_shared_snapshot_valid = true;
#ifdef ESP_PLATFORM
    if (s_snapshot_mutex != nullptr) {
        xSemaphoreGive(s_snapshot_mutex);
    }
#endif

    uart_bms_publish_frame_events(frame, length, &legacy_data);
    uart_bms_publish_live_data(&legacy_data);
    uart_bms_notify_shared_listeners(shared);
    return ESP_OK;
}

esp_err_t uart_bms_write_register(uint16_t address,
                                  uint16_t raw_value,
                                  uint16_t *readback_raw,
                                  uint32_t timeout_ms)
{
#ifdef ESP_PLATFORM
    if (readback_raw != nullptr) {
        *readback_raw = raw_value;
    }

    if (!s_uart_initialised || s_command_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timeout_ms == 0U) {
        timeout_ms = UART_BMS_RESPONSE_TIMEOUT_MS;
    }

    TickType_t semaphore_timeout = pdMS_TO_TICKS(timeout_ms);
    if (semaphore_timeout == 0) {
        semaphore_timeout = 1;
    }

    if (xSemaphoreTake(s_command_mutex, semaphore_timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Utiliser flag au lieu de vTaskSuspend (évite deadlock)
    if (s_uart_poll_task_handle != nullptr) {
        s_poll_pause_requested = true;
        // Attendre que la tâche de poll confirme la pause
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uart_flush_input(UART_BMS_UART_PORT);
    uart_bms_reset_buffer();

    esp_err_t result = ESP_OK;
    uint8_t frame[UART_BMS_MAX_FRAME_SIZE] = {0};
    size_t frame_len = 0;

    esp_err_t build_err = uart_frame_builder_build_write_single(frame,
                                                                 sizeof(frame),
                                                                 address,
                                                                 raw_value,
                                                                 &frame_len);
    if (build_err != ESP_OK) {
        result = build_err;
        goto cleanup;
    }

    int written = uart_write_bytes(UART_BMS_UART_PORT,
                                   reinterpret_cast<const char *>(frame),
                                   frame_len);
    if (written < 0 || (size_t)written != frame_len) {
        ESP_LOGW(kTag, "Failed to send write frame for 0x%04X", (unsigned)address);
        result = ESP_FAIL;
        goto cleanup;
    }

    result = uart_bms_wait_for_ack(timeout_ms);
    if (result != ESP_OK) {
        goto cleanup;
    }

    {
        uint16_t confirmed = raw_value;
        esp_err_t read_err = uart_bms_read_register_blocking(address, timeout_ms, &confirmed);
        if (read_err != ESP_OK) {
            result = read_err;
        } else if (readback_raw != nullptr) {
            *readback_raw = confirmed;
        }
    }

cleanup:
    // Relâcher le flag de pause
    if (s_uart_poll_task_handle != nullptr) {
        s_poll_pause_requested = false;
    }
    xSemaphoreGive(s_command_mutex);
    return result;
#else
    (void)address;
    (void)timeout_ms;
    if (readback_raw != nullptr) {
        *readback_raw = raw_value;
    }
    return ESP_OK;
#endif
}

esp_err_t uart_bms_request_restart(uint32_t timeout_ms)
{
#ifdef ESP_PLATFORM
    return uart_bms_write_register(UART_BMS_SYSTEM_CONTROL_REGISTER,
                                   UART_BMS_SYSTEM_CONTROL_RESTART_VALUE,
                                   NULL,
                                   timeout_ms);
#else
    (void)timeout_ms;
    return ESP_OK;
#endif
}

void uart_bms_get_parser_diagnostics(uart_bms_parser_diagnostics_t *out_diagnostics)
{
    if (out_diagnostics == nullptr) {
        return;
    }
    s_response_parser.getDiagnostics(out_diagnostics);
}

}  // extern "C"

esp_err_t uart_bms_register_shared_listener(uart_bms_shared_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_shared_listeners_mutex == nullptr ||
        xSemaphoreTake(s_shared_listeners_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_ERR_NO_MEM;

    // Vérifier si déjà enregistré
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == callback && s_shared_listeners[i].context == context) {
            result = ESP_OK;
            goto cleanup;
        }
    }

    // Trouver slot libre
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == nullptr) {
            s_shared_listeners[i].callback = callback;
            s_shared_listeners[i].context = context;

            // Appeler immédiatement si snapshot valide (hors mutex pour éviter deadlock)
            bool call_now = s_shared_snapshot_valid;
            TinyBMS_LiveData snapshot_copy = s_shared_snapshot;

            xSemaphoreGive(s_shared_listeners_mutex);

            if (call_now) {
                callback(snapshot_copy, context);
            }

            return ESP_OK;
        }
    }

cleanup:
    xSemaphoreGive(s_shared_listeners_mutex);
    return result;
}

void uart_bms_unregister_shared_listener(uart_bms_shared_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return;
    }

    if (s_shared_listeners_mutex == nullptr ||
        xSemaphoreTake(s_shared_listeners_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(kTag, "Failed to acquire shared listeners mutex for unregister");
        return;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == callback && s_shared_listeners[i].context == context) {
            s_shared_listeners[i].callback = nullptr;
            s_shared_listeners[i].context = nullptr;
        }
    }

    xSemaphoreGive(s_shared_listeners_mutex);
}

const TinyBMS_LiveData *uart_bms_get_latest_shared(void)
{
    // NOTE: This function returns a pointer to shared data. The caller must ensure
    // that the returned pointer is used quickly and not stored, as the data may be
    // updated by the UART polling task at any time. For thread-safe access to all
    // fields, the caller should copy the data while holding appropriate locks.
#ifdef ESP_PLATFORM
    if (s_snapshot_mutex != nullptr) {
        xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    }
#endif

    const TinyBMS_LiveData *result = nullptr;
    if (s_shared_snapshot_valid) {
        result = &s_shared_snapshot;
    }

#ifdef ESP_PLATFORM
    if (s_snapshot_mutex != nullptr) {
        xSemaphoreGive(s_snapshot_mutex);
    }
#endif

    return result;
}

void uart_bms_deinit(void)
{
    if (!s_uart_initialised) {
        return;
    }

    ESP_LOGI(kTag, "Deinitializing UART BMS...");

    // Signal task to exit
    s_task_should_exit = true;

    // Give task time to exit cleanly
    vTaskDelay(pdMS_TO_TICKS(200));

    // Clear all listeners (protégés par mutex appropriés)
    if (s_listeners_mutex != nullptr && xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
            s_listeners[i].callback = nullptr;
            s_listeners[i].context = nullptr;
        }
        xSemaphoreGive(s_listeners_mutex);
    }

    // Clear all shared listeners (mutex séparé)
    if (s_shared_listeners_mutex != nullptr && xSemaphoreTake(s_shared_listeners_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
            s_shared_listeners[i].callback = nullptr;
            s_shared_listeners[i].context = nullptr;
        }
        xSemaphoreGive(s_shared_listeners_mutex);
    }

    // Delete UART driver
    esp_err_t err = uart_driver_delete(UART_BMS_UART_PORT);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to delete UART driver: %s", esp_err_to_name(err));
    }

    // Destroy all mutexes
    if (s_command_mutex != nullptr) {
        vSemaphoreDelete(s_command_mutex);
        s_command_mutex = nullptr;
    }
    if (s_rx_buffer_mutex != nullptr) {
        vSemaphoreDelete(s_rx_buffer_mutex);
        s_rx_buffer_mutex = nullptr;
    }
    if (s_snapshot_mutex != nullptr) {
        vSemaphoreDelete(s_snapshot_mutex);
        s_snapshot_mutex = nullptr;
    }
    if (s_listeners_mutex != nullptr) {
        vSemaphoreDelete(s_listeners_mutex);
        s_listeners_mutex = nullptr;
    }
    if (s_shared_listeners_mutex != nullptr) {
        vSemaphoreDelete(s_shared_listeners_mutex);
        s_shared_listeners_mutex = nullptr;
    }

    // Reset state
    s_uart_initialised = false;
    s_task_should_exit = false;
    s_poll_pause_requested = false;
    s_shared_snapshot_valid = false;
    s_uart_poll_task_handle = nullptr;
    s_event_publisher = nullptr;
    s_poll_request_length = 0;
    s_rx_length = 0;
    s_next_event_buffer = 0;
    s_next_uart_json_buffer = 0;
    s_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;
    std::memset(s_poll_request, 0, sizeof(s_poll_request));
    std::memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
    std::memset(s_event_buffers, 0, sizeof(s_event_buffers));
    std::memset(s_uart_raw_json, 0, sizeof(s_uart_raw_json));
    std::memset(s_uart_decoded_json, 0, sizeof(s_uart_decoded_json));

    ESP_LOGI(kTag, "UART BMS deinitialized");
}

*** End of File
