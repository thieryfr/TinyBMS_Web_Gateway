#include "uart_bms.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

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

#include "app_events.h"
#include "uart_frame_builder.h"

#define UART_BMS_UART_PORT       UART_NUM_1
#define UART_BMS_BAUD_RATE       115200
#define UART_BMS_TX_PIN          GPIO_NUM_17
#define UART_BMS_RX_PIN          GPIO_NUM_16
#define UART_BMS_RX_BUFFER_SIZE  256
#define UART_BMS_TASK_STACK      4096
#define UART_BMS_TASK_PRIORITY   12
#define UART_BMS_MAX_FRAME_SIZE  128
#define UART_BMS_LISTENER_SLOTS  4
#define UART_BMS_EVENT_BUFFERS   4
#define UART_BMS_FRAME_JSON_SIZE 1024
#define UART_BMS_RESPONSE_TIMEOUT_MS 150

static const char *TAG = "uart_bms";

static uint8_t s_poll_request[UART_BMS_MAX_FRAME_SIZE] = {0};
static size_t s_poll_request_length = 0;

static esp_err_t uart_bms_prepare_poll_request(void)
{
    if (s_poll_request_length != 0) {
        return ESP_OK;
    }

    return uart_frame_builder_build_poll_request(s_poll_request,
                                                 sizeof(s_poll_request),
                                                 &s_poll_request_length);
}

typedef struct {
    uart_bms_data_callback_t callback;
    void *context;
} uart_bms_listener_t;

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_listener_t s_listeners[UART_BMS_LISTENER_SLOTS] = {0};
static uart_bms_live_data_t s_event_buffers[UART_BMS_EVENT_BUFFERS];
static size_t s_next_event_buffer = 0;
static char s_uart_raw_json[UART_BMS_EVENT_BUFFERS][UART_BMS_FRAME_JSON_SIZE];
static char s_uart_decoded_json[UART_BMS_EVENT_BUFFERS][UART_BMS_FRAME_JSON_SIZE];
static size_t s_next_uart_json_buffer = 0;
static bool s_uart_initialised = false;
static TaskHandle_t s_uart_poll_task_handle = NULL;
static uint8_t s_rx_buffer[UART_BMS_MAX_FRAME_SIZE] = {0};
static size_t s_rx_length = 0;
#ifdef ESP_PLATFORM
static portMUX_TYPE s_poll_interval_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static uint32_t s_poll_interval_ms = UART_BMS_DEFAULT_POLL_INTERVAL_MS;

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
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

static void uart_bms_notify_listeners(const uart_bms_live_data_t *data)
{
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback != NULL) {
            s_listeners[i].callback(data, s_listeners[i].context);
        }
    }
}

static void uart_bms_publish_live_data(const uart_bms_live_data_t *data)
{
    if (s_event_publisher != NULL) {
        uart_bms_live_data_t *storage = &s_event_buffers[s_next_event_buffer];
        s_next_event_buffer = (s_next_event_buffer + 1) % UART_BMS_EVENT_BUFFERS;
        *storage = *data;

        event_bus_event_t event = {
            .id = APP_EVENT_ID_BMS_LIVE_DATA,
            .payload = storage,
            .payload_size = sizeof(*storage),
        };

        if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
            ESP_LOGW(TAG, "Unable to publish TinyBMS live data event");
        }
    }

    uart_bms_notify_listeners(data);
}

static bool uart_bms_json_append(char *buffer, size_t buffer_size, size_t *offset, const char *fmt, ...)
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

static void uart_bms_publish_frame_events(const uint8_t *frame,
                                          size_t length,
                                          const uart_bms_live_data_t *decoded)
{
    if (s_event_publisher == NULL || frame == NULL || decoded == NULL) {
        return;
    }

    size_t raw_index = s_next_uart_json_buffer;
    s_next_uart_json_buffer = (s_next_uart_json_buffer + 1) % UART_BMS_EVENT_BUFFERS;

    char *raw_json = s_uart_raw_json[raw_index];
    size_t raw_offset = 0;
    if (uart_bms_json_append(raw_json,
                             UART_BMS_FRAME_JSON_SIZE,
                             &raw_offset,
                             "{\"type\":\"uart_raw\",\"timestamp\":%" PRIu64 ",\"length\":%zu,\"data\":\"",
                             decoded->timestamp_ms,
                             length)) {
        for (size_t i = 0; i < length; ++i) {
            if (!uart_bms_json_append(raw_json,
                                      UART_BMS_FRAME_JSON_SIZE,
                                      &raw_offset,
                                      "%02X",
                                      (unsigned)frame[i])) {
                ESP_LOGW(TAG, "UART raw frame JSON truncated");
                raw_offset = 0;
                break;
            }
        }

        if (raw_offset > 0 &&
            uart_bms_json_append(raw_json, UART_BMS_FRAME_JSON_SIZE, &raw_offset, "\"}")) {
            event_bus_event_t raw_event = {
                .id = APP_EVENT_ID_UART_FRAME_RAW,
                .payload = raw_json,
                .payload_size = raw_offset + 1,
            };
            if (!s_event_publisher(&raw_event, pdMS_TO_TICKS(50))) {
                ESP_LOGW(TAG, "Unable to publish UART raw frame event");
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
                              "{\"type\":\"uart_decoded\",\"timestamp\":%" PRIu64 ",\"pack_voltage\":%.3f,"
                              "\"pack_current\":%.3f,\"state_of_charge\":%.2f,\"state_of_health\":%.2f,"
                              "\"average_temperature\":%.2f,\"mos_temperature\":%.2f,\"uptime_seconds\":%" PRIu32 ","
                              "\"cycle_count\":%" PRIu32 ",\"registers\":[",
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
            ESP_LOGW(TAG, "UART decoded frame JSON truncated");
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
        ESP_LOGW(TAG, "UART decoded frame JSON truncated");
        return;
    }

    event_bus_event_t decoded_event = {
        .id = APP_EVENT_ID_UART_FRAME_DECODED,
        .payload = decoded_json,
        .payload_size = decoded_offset + 1,
    };

    if (!s_event_publisher(&decoded_event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Unable to publish UART decoded frame event");
    }
}

static void uart_bms_reset_buffer(void)
{
    s_rx_length = 0;
}

static void uart_bms_consume_bytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (s_rx_length >= sizeof(s_rx_buffer)) {
            ESP_LOGW(TAG, "RX buffer overflow, resetting synchronisation");
            uart_bms_reset_buffer();
        }

        s_rx_buffer[s_rx_length++] = data[i];

        bool progress = true;
        while (progress) {
            progress = false;

            if (s_rx_length < 3) {
                break;
            }

            if (s_rx_buffer[0] != 0xAA) {
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            size_t payload_len = s_rx_buffer[2];
            size_t total_len = payload_len + 5;
            if (total_len > UART_BMS_MAX_FRAME_SIZE) {
                ESP_LOGW(TAG, "Frame length %zu exceeds buffer, dropping byte", total_len);
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            if (s_rx_length < total_len) {
                break;
            }

            esp_err_t err = uart_bms_process_frame(s_rx_buffer, total_len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to process TinyBMS frame: %s", esp_err_to_name(err));
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = true;
                continue;
            }

            if (s_rx_length > total_len) {
                memmove(s_rx_buffer, s_rx_buffer + total_len, s_rx_length - total_len);
            }
            s_rx_length -= total_len;
            progress = (s_rx_length > 0);
        }
    }
}

static void uart_poll_task(void *arg)
{
    (void)arg;
    uint8_t read_buffer[64];

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        esp_err_t frame_err = uart_bms_prepare_poll_request();
        if (frame_err != ESP_OK || s_poll_request_length == 0) {
            ESP_LOGE(TAG,
                     "Unable to prepare TinyBMS poll request: %s",
                     esp_err_to_name(frame_err));
            vTaskDelay(pdMS_TO_TICKS(UART_BMS_MIN_POLL_INTERVAL_MS));
            continue;
        }

        int written = uart_write_bytes(UART_BMS_UART_PORT,
                                       (const char *)s_poll_request,
                                       s_poll_request_length);
        if (written < 0 || (size_t)written != s_poll_request_length) {
            ESP_LOGW(TAG, "Failed to send poll request (%d)", written);
        }

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UART_BMS_RESPONSE_TIMEOUT_MS);
        bool received_bytes = false;
        while (xTaskGetTickCount() < deadline) {
            int bytes_read = uart_read_bytes(UART_BMS_UART_PORT,
                                             read_buffer,
                                             sizeof(read_buffer),
                                             pdMS_TO_TICKS(20));
            if (bytes_read > 0) {
                uart_bms_consume_bytes(read_buffer, (size_t)bytes_read);
                received_bytes = true;
            } else if (bytes_read < 0) {
                ESP_LOGW(TAG, "UART read error: %d", bytes_read);
                break;
            }
        }

        if (!received_bytes) {
            ESP_LOGW(TAG, "TinyBMS poll timed out (no response)");
        }

        uint32_t interval_ms = uart_bms_get_poll_interval_ms();
        TickType_t interval_ticks = pdMS_TO_TICKS(interval_ms);
        if (interval_ticks == 0) {
            interval_ticks = 1;
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

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
        ESP_LOGI(TAG, "TinyBMS poll interval set to %u ms", (unsigned)clamped);
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
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return;
    }

    err = uart_set_pin(UART_BMS_UART_PORT,
                       UART_BMS_TX_PIN,
                       UART_BMS_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return;
    }

    err = uart_driver_install(UART_BMS_UART_PORT,
                              UART_BMS_RX_BUFFER_SIZE,
                              UART_BMS_RX_BUFFER_SIZE,
                              0,
                              NULL,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return;
    }

    s_uart_initialised = true;

    esp_err_t frame_err = uart_bms_prepare_poll_request();
    if (frame_err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialise TinyBMS poll frame: %s", esp_err_to_name(frame_err));
        uart_driver_delete(UART_BMS_UART_PORT);
        s_uart_initialised = false;
        return;
    }

    if (xTaskCreate(uart_poll_task,
                    "uart_poll",
                    UART_BMS_TASK_STACK,
                    NULL,
                    UART_BMS_TASK_PRIORITY,
                    &s_uart_poll_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART BMS task");
        uart_driver_delete(UART_BMS_UART_PORT);
        s_uart_initialised = false;
        s_uart_poll_task_handle = NULL;
    }
}

esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == NULL) {
            s_listeners[i].callback = callback;
            s_listeners[i].context = context;
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == NULL) {
        return;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            s_listeners[i].callback = NULL;
            s_listeners[i].context = NULL;
        }
    }
}

esp_err_t uart_bms_decode_frame(const uint8_t *frame, size_t length, uart_bms_live_data_t *out_data)
{
    if (frame == NULL || out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length < 5) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (frame[0] != 0xAA) {
        return ESP_ERR_INVALID_STATE;
    }

    if (frame[1] != 0x09) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t payload_len = frame[2];
    size_t expected_len = payload_len + 5;
    if (payload_len % 2 != 0 || length < expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t crc_expected = (uint16_t)frame[expected_len - 2] | (uint16_t)(frame[expected_len - 1] << 8);
    uint16_t crc_computed = uart_frame_builder_crc16(frame, expected_len - 2);
    if (crc_expected != crc_computed) {
        return ESP_ERR_INVALID_CRC;
    }

    size_t register_count = payload_len / 2;
    if (register_count > UART_BMS_MAX_REGISTERS) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(out_data, 0, sizeof(*out_data));
    out_data->timestamp_ms = uart_bms_timestamp_ms();
    out_data->register_count = register_count;

    uint16_t raw_words[UART_BMS_MAX_REGISTERS] = {0};

    for (size_t i = 0; i < register_count; ++i) {
        uint16_t raw_value = (uint16_t)frame[3 + i * 2] | (uint16_t)(frame[4 + i * 2] << 8);
        raw_words[i] = raw_value;

        if (i < UART_BMS_REGISTER_WORD_COUNT) {
            uint16_t address = g_uart_bms_poll_addresses[i];
            out_data->registers[i].address = address;
            out_data->registers[i].raw_value = raw_value;
        } else {
            out_data->registers[i].address = 0;
            out_data->registers[i].raw_value = raw_value;
        }
    }

    size_t word_index = 0;
    for (size_t meta_index = 0; meta_index < g_uart_bms_register_count; ++meta_index) {
        const uart_bms_register_metadata_t *meta = &g_uart_bms_registers[meta_index];
        if (word_index + meta->word_count > register_count) {
            break;
        }

        switch (meta->type) {
            case UART_BMS_VALUE_UINT16: {
                uint16_t raw = raw_words[word_index];
                float scaled = (float)raw * meta->scale;

                switch (meta->primary_field) {
                    case UART_BMS_FIELD_MIN_CELL_MV:
                        out_data->min_cell_mv = raw;
                        break;
                    case UART_BMS_FIELD_MAX_CELL_MV:
                        out_data->max_cell_mv = raw;
                        break;
                    case UART_BMS_FIELD_STATE_OF_HEALTH:
                        out_data->state_of_health_pct = scaled;
                        break;
                    case UART_BMS_FIELD_SYSTEM_STATUS:
                        out_data->alarm_bits = raw;
                        break;
                    case UART_BMS_FIELD_NEED_BALANCING:
                        out_data->warning_bits = raw;
                        break;
                    case UART_BMS_FIELD_BALANCING_BITS:
                        out_data->balancing_bits = raw;
                        break;
                    case UART_BMS_FIELD_PEAK_DISCHARGE_CURRENT_LIMIT:
                        out_data->peak_discharge_current_limit_a = scaled;
                        break;
                    case UART_BMS_FIELD_BATTERY_CAPACITY:
                        out_data->battery_capacity_ah = scaled;
                        break;
                    case UART_BMS_FIELD_SERIES_CELL_COUNT:
                        out_data->series_cell_count = raw;
                        break;
                    case UART_BMS_FIELD_OVERVOLTAGE_CUTOFF:
                        out_data->overvoltage_cutoff_mv = raw;
                        break;
                    case UART_BMS_FIELD_UNDERVOLTAGE_CUTOFF:
                        out_data->undervoltage_cutoff_mv = raw;
                        break;
                    case UART_BMS_FIELD_DISCHARGE_OVER_CURRENT_LIMIT:
                        out_data->discharge_overcurrent_limit_a = scaled;
                        break;
                    case UART_BMS_FIELD_CHARGE_OVER_CURRENT_LIMIT:
                        out_data->charge_overcurrent_limit_a = scaled;
                        break;
                    case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                        out_data->overheat_cutoff_c = scaled;
                        break;
                    case UART_BMS_FIELD_HARDWARE_VERSION:
                        out_data->hardware_version = (uint8_t)(raw & 0xFF);
                        if (meta->secondary_field == UART_BMS_FIELD_HARDWARE_CHANGES_VERSION) {
                            out_data->hardware_changes_version = (uint8_t)((raw >> 8) & 0xFF);
                        }
                        break;
                    case UART_BMS_FIELD_FIRMWARE_VERSION:
                        out_data->firmware_version = (uint8_t)(raw & 0xFF);
                        if (meta->secondary_field == UART_BMS_FIELD_FIRMWARE_FLAGS) {
                            out_data->firmware_flags = (uint8_t)((raw >> 8) & 0xFF);
                        }
                        break;
                    case UART_BMS_FIELD_INTERNAL_FIRMWARE_VERSION:
                        out_data->internal_firmware_version = raw;
                        break;
                    default:
                        break;
                }

                ++word_index;
                break;
            }
            case UART_BMS_VALUE_INT16: {
                int16_t raw = (int16_t)raw_words[word_index];
                float scaled = (float)raw * meta->scale;

                switch (meta->primary_field) {
                    case UART_BMS_FIELD_AVERAGE_TEMPERATURE:
                        out_data->average_temperature_c = scaled;
                        break;
                    case UART_BMS_FIELD_AUXILIARY_TEMPERATURE:
                        out_data->auxiliary_temperature_c = scaled;
                        break;
                    case UART_BMS_FIELD_MOS_TEMPERATURE:
                        out_data->mosfet_temperature_c = scaled;
                        break;
                    case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                        out_data->overheat_cutoff_c = scaled;
                        break;
                    default:
                        break;
                }

                ++word_index;
                break;
            }
            case UART_BMS_VALUE_UINT32: {
                uint32_t raw = (uint32_t)raw_words[word_index] |
                                ((uint32_t)raw_words[word_index + 1] << 16);
                float scaled = (float)raw * meta->scale;

                switch (meta->primary_field) {
                    case UART_BMS_FIELD_STATE_OF_CHARGE:
                        out_data->state_of_charge_pct = scaled;
                        break;
                    case UART_BMS_FIELD_UPTIME_SECONDS:
                        out_data->uptime_seconds = raw;
                        break;
                    default:
                        break;
                }

                word_index += meta->word_count;
                break;
            }
            case UART_BMS_VALUE_FLOAT32: {
                uint32_t raw = (uint32_t)raw_words[word_index] |
                                ((uint32_t)raw_words[word_index + 1] << 16);
                float value;
                memcpy(&value, &raw, sizeof(value));
                value *= meta->scale;

                switch (meta->primary_field) {
                    case UART_BMS_FIELD_PACK_VOLTAGE:
                        out_data->pack_voltage_v = value;
                        break;
                    case UART_BMS_FIELD_PACK_CURRENT:
                        out_data->pack_current_a = value;
                        break;
                    default:
                        break;
                }

                word_index += meta->word_count;
                break;
            }
            case UART_BMS_VALUE_INT8_PAIR: {
                uint16_t raw = raw_words[word_index];
                int8_t low = (int8_t)(raw & 0xFF);
                int8_t high = (int8_t)((raw >> 8) & 0xFF);
                float low_scaled = (float)low * meta->scale;
                float high_scaled = (float)high * meta->scale;

                if (meta->primary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MIN) {
                    out_data->pack_temperature_min_c = low_scaled;
                }
                if (meta->secondary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MAX) {
                    out_data->pack_temperature_max_c = high_scaled;
                }

                ++word_index;
                break;
            }
            default:
                ++word_index;
                break;
        }
    }

    return ESP_OK;
}

esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length)
{
    uart_bms_live_data_t data;
    esp_err_t err = uart_bms_decode_frame(frame, length, &data);
    if (err != ESP_OK) {
        return err;
    }

    uart_bms_publish_frame_events(frame, length, &data);
    uart_bms_publish_live_data(&data);
    return ESP_OK;
}
*** End of File
