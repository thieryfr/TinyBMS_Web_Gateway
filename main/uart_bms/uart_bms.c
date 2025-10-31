#include "uart_bms.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#else
#include <sys/time.h>
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE ESP_ERR_INVALID_STATE
#endif

#define UART_BMS_FRAME_HEADER 0xAA
#define UART_BMS_FRAME_FUNCTION 0x09
#define UART_BMS_HEADER_SIZE 3
#define UART_BMS_CRC_SIZE 2

#define UART_BMS_TASK_STACK_SIZE 4096
#define UART_BMS_TASK_PRIORITY 12
#define UART_BMS_RX_BUFFER_SIZE 256
#define UART_BMS_LISTENER_CAPACITY 4
#define UART_BMS_POLL_TIMEOUT_MS 200

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifdef ESP_PLATFORM
#define UART_BMS_DEFAULT_PORT ((int)UART_NUM_1)
#define UART_BMS_DEFAULT_TX_PIN ((int)GPIO_NUM_17)
#define UART_BMS_DEFAULT_RX_PIN ((int)GPIO_NUM_16)
#else
#define UART_BMS_DEFAULT_PORT 1
#define UART_BMS_DEFAULT_TX_PIN 17
#define UART_BMS_DEFAULT_RX_PIN 16
#endif

#define UART_BMS_DEFAULT_BAUDRATE 115200
#define UART_BMS_DEFAULT_POLL_INTERVAL pdMS_TO_TICKS(1000)

typedef struct {
    uart_bms_data_callback_t callback;
    void *context;
} uart_bms_listener_entry_t;

static const char *TAG = "uart_bms";

static const uint16_t s_register_addresses[UART_BMS_MAX_REGISTERS] = {
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028,
    0x0029, 0x002A, 0x0066, 0x0067, 0x0071, 0x0072, 0x0131, 0x0132, 0x0133,
    0x013B, 0x013C, 0x013D, 0x013E, 0x013F, 0x0140, 0x0141, 0x0142, 0x0143,
    0x0144, 0x0145, 0x0146, 0x0147, 0x0148, 0x0149, 0x014A, 0x014B, 0x014C,
    0x014D, 0x014E, 0x014F,
};

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_config_t s_active_config = {0};
static bool s_config_initialised = false;
static bool s_driver_started = false;
static TaskHandle_t s_poll_task = NULL;
static bool s_stop_task = false;
static uart_bms_live_data_t s_latest_sample = {0};
static uart_bms_listener_entry_t s_listeners[UART_BMS_LISTENER_CAPACITY] = {0};
static SemaphoreHandle_t s_listener_mutex = NULL;
static StaticSemaphore_t s_listener_mutex_buffer;

static uint16_t uart_bms_compute_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
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

static void uart_bms_copy_registers(uart_bms_live_data_t *data, const uint16_t *values, size_t value_count)
{
    size_t to_copy = value_count;
    if (to_copy > UART_BMS_MAX_REGISTERS) {
        to_copy = UART_BMS_MAX_REGISTERS;
    }

    data->register_count = to_copy;
    for (size_t i = 0; i < to_copy; ++i) {
        data->registers[i].address = s_register_addresses[i];
        data->registers[i].raw_value = values[i];
    }
}

static void uart_bms_decode_fields(uart_bms_live_data_t *data, const uint16_t *values, size_t value_count)
{
    if (value_count > 0) {
        data->pack_voltage_valid = true;
        data->pack_voltage_v = (float)values[0] / 100.0f;
    }

    if (value_count > 1) {
        data->pack_current_valid = true;
        data->pack_current_a = (float)(int16_t)values[1] / 10.0f;
    }

    if (value_count > 2) {
        data->min_cell_mv_valid = true;
        data->min_cell_mv = values[2];
    }

    if (value_count > 3) {
        data->max_cell_mv_valid = true;
        data->max_cell_mv = values[3];
    }

    if (value_count > 4) {
        data->state_of_charge_valid = true;
        data->state_of_charge_pct = (float)values[4] / 100.0f;
    }

    if (value_count > 5) {
        data->state_of_health_valid = true;
        data->state_of_health_pct = (float)values[5] / 100.0f;
    }

    if (value_count > 6) {
        data->average_temperature_valid = true;
        data->average_temperature_c = (float)(int16_t)values[6] / 10.0f;
    }

    if (value_count > 7) {
        data->mosfet_temperature_valid = true;
        data->mosfet_temperature_c = (float)(int16_t)values[7] / 10.0f;
    }

    if (value_count > 8) {
        data->balancing_bits_valid = true;
        data->balancing_bits = values[8];
    }

    if (value_count > 9) {
        data->alarm_bits_valid = true;
        data->alarm_bits = values[9];
    }

    if (value_count > 10) {
        data->warning_bits_valid = true;
        data->warning_bits = values[10];
    }

    if (value_count > 22) {
        data->uptime_valid = true;
        data->uptime_seconds = ((uint32_t)values[22] << 16) | values[21];
    }

    if (value_count > 24) {
        data->cycle_count_valid = true;
        data->cycle_count = ((uint32_t)values[24] << 16) | values[23];
    }
}

static void uart_bms_publish_sample(const uart_bms_live_data_t *sample)
{
    if (sample == NULL) {
        return;
    }

    if (s_listener_mutex == NULL) {
        s_listener_mutex = xSemaphoreCreateMutexStatic(&s_listener_mutex_buffer);
    }

    bool copied_to_latest = false;
    if (xSemaphoreTake(s_listener_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_latest_sample = *sample;
        copied_to_latest = true;
        for (size_t i = 0; i < UART_BMS_LISTENER_CAPACITY; ++i) {
            if (s_listeners[i].callback != NULL) {
                s_listeners[i].callback(&s_latest_sample, s_listeners[i].context);
            }
        }
        xSemaphoreGive(s_listener_mutex);
    }

    if (!copied_to_latest) {
        s_latest_sample = *sample;
    }

    if (s_event_publisher != NULL) {
        event_bus_event_t event = {
            .id = UART_BMS_EVENT_ID_LIVE_DATA,
            .payload = &s_latest_sample,
            .payload_size = sizeof(uart_bms_live_data_t),
        };

        if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
            ESP_LOGW(TAG, "Unable to publish TinyBMS telemetry event");
        }
    }
}

static size_t uart_bms_consume_frames(uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (length - offset >= UART_BMS_HEADER_SIZE + UART_BMS_CRC_SIZE) {
        if (buffer[offset] != UART_BMS_FRAME_HEADER) {
            ++offset;
            continue;
        }

        if (length - offset < UART_BMS_HEADER_SIZE) {
            break;
        }

        if (buffer[offset + 1] != UART_BMS_FRAME_FUNCTION) {
            ++offset;
            continue;
        }

        uint8_t payload_len = buffer[offset + 2];
        size_t frame_len = UART_BMS_HEADER_SIZE + payload_len + UART_BMS_CRC_SIZE;
        if (length - offset < frame_len) {
            break;
        }

        esp_err_t err = uart_bms_process_frame(&buffer[offset], frame_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Dropping TinyBMS frame: %s", esp_err_to_name(err));
        }

        offset += frame_len;
    }

    return offset;
}

#ifdef ESP_PLATFORM
static size_t uart_bms_build_poll_request(uint8_t *buffer, size_t buffer_size)
{
    size_t payload_len = ARRAY_SIZE(s_register_addresses) * sizeof(uint16_t);
    size_t frame_len = UART_BMS_HEADER_SIZE + payload_len + UART_BMS_CRC_SIZE;
    if (buffer_size < frame_len) {
        return 0;
    }

    buffer[0] = UART_BMS_FRAME_HEADER;
    buffer[1] = UART_BMS_FRAME_FUNCTION;
    buffer[2] = (uint8_t)payload_len;

    for (size_t i = 0; i < ARRAY_SIZE(s_register_addresses); ++i) {
        uint16_t reg = s_register_addresses[i];
        buffer[UART_BMS_HEADER_SIZE + i * 2] = (uint8_t)(reg & 0xFF);
        buffer[UART_BMS_HEADER_SIZE + i * 2 + 1] = (uint8_t)(reg >> 8);
    }

    uint16_t crc = uart_bms_compute_crc(buffer, UART_BMS_HEADER_SIZE + payload_len);
    buffer[frame_len - 2] = (uint8_t)(crc & 0xFF);
    buffer[frame_len - 1] = (uint8_t)(crc >> 8);

    return frame_len;
}
#endif

static void uart_bms_poll_task(void *arg)
{
    (void)arg;

    uint8_t request[UART_BMS_HEADER_SIZE + UART_BMS_MAX_REGISTERS * sizeof(uint16_t) + UART_BMS_CRC_SIZE] = {0};
    uint8_t rx_buffer[UART_BMS_RX_BUFFER_SIZE] = {0};
    size_t rx_length = 0;

    while (!s_stop_task) {
#ifdef ESP_PLATFORM
        size_t request_len = uart_bms_build_poll_request(request, sizeof(request));
        if (request_len > 0) {
            int written = uart_write_bytes(s_active_config.uart_port, (const char *)request, request_len);
            if (written < 0) {
                ESP_LOGW(TAG, "Failed to write TinyBMS poll request");
            }
        }

        int read = uart_read_bytes(s_active_config.uart_port,
                                   rx_buffer + rx_length,
                                   sizeof(rx_buffer) - rx_length,
                                   pdMS_TO_TICKS(UART_BMS_POLL_TIMEOUT_MS));
        if (read > 0) {
            rx_length += (size_t)read;
            size_t consumed = uart_bms_consume_frames(rx_buffer, rx_length);
            if (consumed > 0) {
                memmove(rx_buffer, rx_buffer + consumed, rx_length - consumed);
                rx_length -= consumed;
            }
        }
#endif

        vTaskDelay(s_active_config.poll_interval);
    }

    s_poll_task = NULL;
    vTaskDelete(NULL);
}

void uart_bms_get_default_config(uart_bms_config_t *out_config)
{
    if (out_config == NULL) {
        return;
    }

    out_config->uart_port = UART_BMS_DEFAULT_PORT;
    out_config->tx_pin = UART_BMS_DEFAULT_TX_PIN;
    out_config->rx_pin = UART_BMS_DEFAULT_RX_PIN;
    out_config->baud_rate = UART_BMS_DEFAULT_BAUDRATE;
    out_config->poll_interval = UART_BMS_DEFAULT_POLL_INTERVAL;
}

void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void uart_bms_apply_config(const uart_bms_config_t *config)
{
    if (config == NULL) {
        return;
    }

    s_active_config = *config;
    s_config_initialised = true;
}

static void uart_bms_ensure_config(void)
{
    if (!s_config_initialised) {
        uart_bms_get_default_config(&s_active_config);
        s_config_initialised = true;
    }
}

esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_listener_mutex == NULL) {
        s_listener_mutex = xSemaphoreCreateMutexStatic(&s_listener_mutex_buffer);
    }

    if (xSemaphoreTake(s_listener_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_CAPACITY; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            xSemaphoreGive(s_listener_mutex);
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_CAPACITY; ++i) {
        if (s_listeners[i].callback == NULL) {
            s_listeners[i].callback = callback;
            s_listeners[i].context = context;
            xSemaphoreGive(s_listener_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_listener_mutex);
    return ESP_ERR_NO_MEM;
}

void uart_bms_unregister_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == NULL || s_listener_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_listener_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_CAPACITY; ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            s_listeners[i].callback = NULL;
            s_listeners[i].context = NULL;
            break;
        }
    }

    xSemaphoreGive(s_listener_mutex);
}

void uart_bms_init(void)
{
    if (s_driver_started) {
        return;
    }

    uart_bms_ensure_config();

    if (s_listener_mutex == NULL) {
        s_listener_mutex = xSemaphoreCreateMutexStatic(&s_listener_mutex_buffer);
    }

#ifdef ESP_PLATFORM
    uart_config_t uart_config = {
        .baud_rate = s_active_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_param_config(s_active_config.uart_port, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to configure UART%u", (unsigned)s_active_config.uart_port);
        return;
    }

    if (uart_set_pin(s_active_config.uart_port,
                     s_active_config.tx_pin,
                     s_active_config.rx_pin,
                     UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to configure UART pins");
        return;
    }

    if (uart_driver_install(s_active_config.uart_port, UART_BMS_RX_BUFFER_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to install UART driver");
        return;
    }
#endif

    s_stop_task = false;
    if (xTaskCreate(uart_bms_poll_task,
                    "tinybms_uart",
                    UART_BMS_TASK_STACK_SIZE,
                    NULL,
                    UART_BMS_TASK_PRIORITY,
                    &s_poll_task) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TinyBMS poll task");
#ifdef ESP_PLATFORM
        uart_driver_delete(s_active_config.uart_port);
#endif
        s_poll_task = NULL;
        return;
    }

    s_driver_started = true;
    ESP_LOGI(TAG, "TinyBMS UART driver initialised on UART%u", (unsigned)s_active_config.uart_port);
}

void uart_bms_deinit(void)
{
    if (!s_driver_started) {
        return;
    }

    s_stop_task = true;
    TaskHandle_t task = s_poll_task;
    s_poll_task = NULL;
    if (task != NULL) {
        vTaskDelete(task);
    }

#ifdef ESP_PLATFORM
    uart_driver_delete(s_active_config.uart_port);
#endif

    s_driver_started = false;
}

esp_err_t uart_bms_process_frame(const uint8_t *frame, size_t length)
{
    if (frame == NULL || length < UART_BMS_HEADER_SIZE + UART_BMS_CRC_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    if (frame[0] != UART_BMS_FRAME_HEADER || frame[1] != UART_BMS_FRAME_FUNCTION) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t payload_len = frame[2];
    if ((size_t)payload_len + UART_BMS_HEADER_SIZE + UART_BMS_CRC_SIZE != length) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t expected_crc = uart_bms_compute_crc(frame, length - UART_BMS_CRC_SIZE);
    uint16_t frame_crc = (uint16_t)frame[length - 1] << 8 | frame[length - 2];
    if (expected_crc != frame_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    uart_bms_live_data_t decoded = {0};
    esp_err_t err = uart_bms_decode_frame(frame, length, &decoded);
    if (err != ESP_OK) {
        return err;
    }

    uart_bms_publish_sample(&decoded);
    return ESP_OK;
}

esp_err_t uart_bms_decode_frame(const uint8_t *frame,
                                size_t length,
                                uart_bms_live_data_t *out_data)
{
    if (frame == NULL || out_data == NULL || length < UART_BMS_HEADER_SIZE + UART_BMS_CRC_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload_len = frame[2];
    size_t frame_len = UART_BMS_HEADER_SIZE + payload_len + UART_BMS_CRC_SIZE;
    if (frame_len != length) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t value_count = payload_len / sizeof(uint16_t);
    uint16_t values[UART_BMS_MAX_REGISTERS] = {0};
    for (size_t i = 0; i < value_count && i < UART_BMS_MAX_REGISTERS; ++i) {
        size_t index = UART_BMS_HEADER_SIZE + i * 2;
        values[i] = (uint16_t)frame[index] | ((uint16_t)frame[index + 1] << 8);
    }

    memset(out_data, 0, sizeof(*out_data));
    out_data->timestamp_ms = uart_bms_timestamp_ms();

    uart_bms_copy_registers(out_data, values, value_count);
    uart_bms_decode_fields(out_data, values, value_count);

    return ESP_OK;
}

