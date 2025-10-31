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
#include "freertos/task.h"

#define UART_BMS_FRAME_HEADER 0xAA
#define UART_BMS_FRAME_FUNCTION 0x09

#define UART_BMS_LISTENER_CAPACITY 4
#define UART_BMS_EVENT_BUFFER_COUNT 2
#define UART_BMS_RX_BUFFER_SIZE 256
#define UART_BMS_TASK_STACK_SIZE 4096
#define UART_BMS_TASK_PRIORITY 12
#define UART_BMS_POLL_TIMEOUT_MS 200
#define UART_BMS_DEFAULT_POLL_INTERVAL_MS 1000

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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef enum {
    UART_BMS_FIELD_NONE = 0,
    UART_BMS_FIELD_PACK_VOLTAGE,
    UART_BMS_FIELD_PACK_CURRENT,
    UART_BMS_FIELD_MIN_CELL,
    UART_BMS_FIELD_MAX_CELL,
    UART_BMS_FIELD_SOC,
    UART_BMS_FIELD_SOH,
    UART_BMS_FIELD_AVG_TEMP,
    UART_BMS_FIELD_MOS_TEMP,
    UART_BMS_FIELD_BALANCING,
    UART_BMS_FIELD_ALARM,
    UART_BMS_FIELD_WARNING,
    UART_BMS_FIELD_UPTIME_LOW,
    UART_BMS_FIELD_UPTIME_HIGH,
    UART_BMS_FIELD_CYCLE_LOW,
    UART_BMS_FIELD_CYCLE_HIGH,
} uart_bms_field_t;

typedef struct {
    uart_bms_data_callback_t callback;
    void *context;
} uart_bms_listener_t;

typedef struct {
    uint16_t address;
    bool is_signed;
    float scale;
    uart_bms_field_t field;
} uart_bms_register_descriptor_t;

static const char *TAG = "uart_bms";

static const uint16_t s_poll_addresses[UART_BMS_MAX_REGISTERS] = {
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028,
    0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F, 0x0030, 0x0031,
    0x0032, 0x0033, 0x0034, 0x0066, 0x0067, 0x0071, 0x0072, 0x0131, 0x0132,
    0x0133, 0x013B, 0x013C, 0x013D, 0x013E, 0x013F, 0x01F4, 0x01F5, 0x01F6,
    0x01F7, 0x01F8, 0x01F9,
};

static const uart_bms_register_descriptor_t s_register_descriptors[] = {
    {0x0020, false, 0.01f, UART_BMS_FIELD_PACK_VOLTAGE},
    {0x0021, true, 0.1f, UART_BMS_FIELD_PACK_CURRENT},
    {0x0022, false, 1.0f, UART_BMS_FIELD_MIN_CELL},
    {0x0023, false, 1.0f, UART_BMS_FIELD_MAX_CELL},
    {0x0024, false, 0.01f, UART_BMS_FIELD_SOC},
    {0x0025, false, 0.01f, UART_BMS_FIELD_SOH},
    {0x0026, true, 0.1f, UART_BMS_FIELD_AVG_TEMP},
    {0x0027, true, 0.1f, UART_BMS_FIELD_MOS_TEMP},
    {0x0028, false, 1.0f, UART_BMS_FIELD_BALANCING},
    {0x0029, false, 1.0f, UART_BMS_FIELD_ALARM},
    {0x002A, false, 1.0f, UART_BMS_FIELD_WARNING},
    {0x0066, false, 1.0f, UART_BMS_FIELD_UPTIME_LOW},
    {0x0067, false, 1.0f, UART_BMS_FIELD_UPTIME_HIGH},
    {0x0071, false, 1.0f, UART_BMS_FIELD_CYCLE_LOW},
    {0x0072, false, 1.0f, UART_BMS_FIELD_CYCLE_HIGH},
};

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_listener_t s_listeners[UART_BMS_LISTENER_CAPACITY];
static uart_bms_live_data_t s_event_buffers[UART_BMS_EVENT_BUFFER_COUNT];
static size_t s_next_event_buffer = 0;
static uart_bms_config_t s_active_config;
static bool s_config_set = false;
static bool s_uart_initialised = false;
static TaskHandle_t s_uart_task_handle = NULL;
static uint8_t s_rx_buffer[UART_BMS_RX_BUFFER_SIZE];
static size_t s_rx_length = 0;
static bool s_poll_request_ready = false;
static size_t s_poll_request_length = 0;
static uint8_t s_poll_request[3 + sizeof(s_poll_addresses) + 2];

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

static const uart_bms_register_descriptor_t *find_descriptor(uint16_t address)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_register_descriptors); ++i) {
        if (s_register_descriptors[i].address == address) {
            return &s_register_descriptors[i];
        }
    }
    return NULL;
}

static void uart_bms_prepare_poll_request(void)
{
    if (s_poll_request_ready) {
        return;
    }

    size_t payload_len = sizeof(s_poll_addresses);
    s_poll_request[0] = UART_BMS_FRAME_HEADER;
    s_poll_request[1] = UART_BMS_FRAME_FUNCTION;
    s_poll_request[2] = (uint8_t)payload_len;

    for (size_t i = 0; i < ARRAY_SIZE(s_poll_addresses); ++i) {
        uint16_t address = s_poll_addresses[i];
        s_poll_request[3 + i * 2] = (uint8_t)(address & 0xFF);
        s_poll_request[4 + i * 2] = (uint8_t)(address >> 8);
    }

    size_t frame_len_without_crc = 3 + payload_len;
    uint16_t crc = uart_bms_compute_crc(s_poll_request, frame_len_without_crc);
    s_poll_request[frame_len_without_crc] = (uint8_t)(crc & 0xFF);
    s_poll_request[frame_len_without_crc + 1] = (uint8_t)(crc >> 8);
    s_poll_request_length = frame_len_without_crc + 2;
    s_poll_request_ready = true;
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
    for (size_t i = 0; i < ARRAY_SIZE(s_listeners); ++i) {
        if (s_listeners[i].callback != NULL) {
            s_listeners[i].callback(data, s_listeners[i].context);
        }
    }
}

static void uart_bms_publish_live_data(const uart_bms_live_data_t *data)
{
    if (data == NULL) {
        return;
    }

    if (s_event_publisher == NULL) {
        uart_bms_notify_listeners(data);
        return;
    }

    uart_bms_live_data_t *storage = &s_event_buffers[s_next_event_buffer];
    s_next_event_buffer = (s_next_event_buffer + 1) % UART_BMS_EVENT_BUFFER_COUNT;
    *storage = *data;

    event_bus_event_t event = {
        .id = UART_BMS_EVENT_ID_LIVE_DATA,
        .payload = storage,
        .payload_size = sizeof(*storage),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Unable to publish TinyBMS telemetry event");
    }

    uart_bms_notify_listeners(storage);
}

static void uart_bms_reset_buffer(void)
{
    s_rx_length = 0;
}

static void uart_bms_consume_bytes(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        if (s_rx_length >= sizeof(s_rx_buffer)) {
            ESP_LOGW(TAG, "TinyBMS RX buffer overflow");
            uart_bms_reset_buffer();
        }

        s_rx_buffer[s_rx_length++] = data[i];

        bool progress = true;
        while (progress) {
            progress = false;

            if (s_rx_length < 3) {
                break;
            }

            if (s_rx_buffer[0] != UART_BMS_FRAME_HEADER) {
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = (s_rx_length > 0);
                continue;
            }

            uint8_t payload_len = s_rx_buffer[2];
            size_t frame_len = (size_t)payload_len + 5;

            if ((payload_len % 2) != 0 || frame_len > UART_BMS_RX_BUFFER_SIZE) {
                ESP_LOGW(TAG, "Dropping TinyBMS frame with invalid length");
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = (s_rx_length > 0);
                continue;
            }

            if (s_rx_length < frame_len) {
                break;
            }

            esp_err_t err = uart_bms_process_frame(s_rx_buffer, frame_len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Discarding TinyBMS frame: %s", esp_err_to_name(err));
            }

            if (s_rx_length > frame_len) {
                memmove(s_rx_buffer, s_rx_buffer + frame_len, s_rx_length - frame_len);
            }
            s_rx_length -= frame_len;
            progress = (s_rx_length > 0);
        }
    }
}

#ifdef ESP_PLATFORM
static void uart_bms_task(void *arg)
{
    (void)arg;
    uint8_t read_buffer[64];

    const TickType_t poll_interval = (s_active_config.poll_interval != 0)
                                         ? s_active_config.poll_interval
                                         : pdMS_TO_TICKS(UART_BMS_DEFAULT_POLL_INTERVAL_MS);

    while (true) {
        int written = uart_write_bytes((uart_port_t)s_active_config.uart_port,
                                       (const char *)s_poll_request,
                                       s_poll_request_length);
        if (written < 0) {
            ESP_LOGW(TAG, "Unable to send TinyBMS poll request");
        }

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UART_BMS_POLL_TIMEOUT_MS);
        while (xTaskGetTickCount() < deadline) {
            int read = uart_read_bytes((uart_port_t)s_active_config.uart_port,
                                       read_buffer,
                                       sizeof(read_buffer),
                                       pdMS_TO_TICKS(20));
            if (read > 0) {
                uart_bms_consume_bytes(read_buffer, (size_t)read);
            } else if (read == 0) {
                continue;
            } else {
                ESP_LOGW(TAG, "TinyBMS UART read failed");
                break;
            }
        }

        vTaskDelay(poll_interval);
    }
}
#endif

void uart_bms_get_default_config(uart_bms_config_t *out_config)
{
    if (out_config == NULL) {
        return;
    }

    out_config->uart_port = UART_BMS_DEFAULT_PORT;
    out_config->tx_pin = UART_BMS_DEFAULT_TX_PIN;
    out_config->rx_pin = UART_BMS_DEFAULT_RX_PIN;
    out_config->baud_rate = UART_BMS_DEFAULT_BAUDRATE;
    out_config->poll_interval = pdMS_TO_TICKS(UART_BMS_DEFAULT_POLL_INTERVAL_MS);
}

void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void uart_bms_apply_config(const uart_bms_config_t *config)
{
    if (config == NULL) {
        uart_bms_get_default_config(&s_active_config);
    } else {
        s_active_config = *config;
        if (s_active_config.poll_interval == 0) {
            s_active_config.poll_interval = pdMS_TO_TICKS(UART_BMS_DEFAULT_POLL_INTERVAL_MS);
        }
    }

    s_config_set = true;
}

void uart_bms_init(void)
{
    if (!s_config_set) {
        uart_bms_apply_config(NULL);
    }

    if (s_uart_initialised) {
        return;
    }

    uart_bms_prepare_poll_request();

#ifdef ESP_PLATFORM
    uart_config_t config = {
        .baud_rate = s_active_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config((uart_port_t)s_active_config.uart_port, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return;
    }

    err = uart_set_pin((uart_port_t)s_active_config.uart_port,
                       s_active_config.tx_pin,
                       s_active_config.rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return;
    }

    err = uart_driver_install((uart_port_t)s_active_config.uart_port,
                              UART_BMS_RX_BUFFER_SIZE,
                              0,
                              0,
                              NULL,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(uart_bms_task,
                    "uart_bms_rx",
                    UART_BMS_TASK_STACK_SIZE,
                    NULL,
                    UART_BMS_TASK_PRIORITY,
                    &s_uart_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TinyBMS UART task");
        uart_driver_delete((uart_port_t)s_active_config.uart_port);
        s_uart_task_handle = NULL;
        return;
    }
#endif

    s_uart_initialised = true;
}

void uart_bms_deinit(void)
{
#ifdef ESP_PLATFORM
    if (s_uart_task_handle != NULL) {
        TaskHandle_t handle = s_uart_task_handle;
        s_uart_task_handle = NULL;
        vTaskDelete(handle);
    }

    if (s_uart_initialised) {
        uart_driver_delete((uart_port_t)s_active_config.uart_port);
    }
#endif

    s_uart_initialised = false;
    uart_bms_reset_buffer();
}

esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < ARRAY_SIZE(s_listeners); ++i) {
        if (s_listeners[i].callback == callback && s_listeners[i].context == context) {
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(s_listeners); ++i) {
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

    for (size_t i = 0; i < ARRAY_SIZE(s_listeners); ++i) {
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

    uint8_t payload_len = frame[2];
    size_t expected_len = (size_t)payload_len + 5;

    if (frame[0] != UART_BMS_FRAME_HEADER || frame[1] != UART_BMS_FRAME_FUNCTION) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((payload_len % 2) != 0 || expected_len != length) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t expected_crc = (uint16_t)frame[expected_len - 2] | (uint16_t)(frame[expected_len - 1] << 8);
    uint16_t computed_crc = uart_bms_compute_crc(frame, expected_len - 2);
    if (expected_crc != computed_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    size_t register_count = payload_len / 2;
    if (register_count > UART_BMS_MAX_REGISTERS) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(out_data, 0, sizeof(*out_data));
    out_data->timestamp_ms = uart_bms_timestamp_ms();
    out_data->register_count = register_count;

    uint16_t uptime_low = 0;
    uint16_t uptime_high = 0;
    bool uptime_low_valid = false;
    bool uptime_high_valid = false;
    uint16_t cycle_low = 0;
    uint16_t cycle_high = 0;
    bool cycle_low_valid = false;
    bool cycle_high_valid = false;

    for (size_t i = 0; i < register_count; ++i) {
        uint16_t raw_value = (uint16_t)frame[3 + i * 2] | (uint16_t)(frame[4 + i * 2] << 8);

        uint16_t address = 0;
        if (i < ARRAY_SIZE(s_poll_addresses)) {
            address = s_poll_addresses[i];
        }

        out_data->registers[i].address = address;
        out_data->registers[i].raw_value = raw_value;

        const uart_bms_register_descriptor_t *descriptor = find_descriptor(address);
        if (descriptor == NULL) {
            continue;
        }

        int32_t signed_value = descriptor->is_signed ? (int32_t)(int16_t)raw_value : (int32_t)raw_value;
        float scaled = (float)signed_value * descriptor->scale;

        switch (descriptor->field) {
            case UART_BMS_FIELD_PACK_VOLTAGE:
                out_data->pack_voltage_valid = true;
                out_data->pack_voltage_v = scaled;
                break;
            case UART_BMS_FIELD_PACK_CURRENT:
                out_data->pack_current_valid = true;
                out_data->pack_current_a = scaled;
                break;
            case UART_BMS_FIELD_MIN_CELL:
                out_data->min_cell_mv_valid = true;
                out_data->min_cell_mv = (uint16_t)signed_value;
                break;
            case UART_BMS_FIELD_MAX_CELL:
                out_data->max_cell_mv_valid = true;
                out_data->max_cell_mv = (uint16_t)signed_value;
                break;
            case UART_BMS_FIELD_SOC:
                out_data->state_of_charge_valid = true;
                out_data->state_of_charge_pct = scaled;
                break;
            case UART_BMS_FIELD_SOH:
                out_data->state_of_health_valid = true;
                out_data->state_of_health_pct = scaled;
                break;
            case UART_BMS_FIELD_AVG_TEMP:
                out_data->average_temperature_valid = true;
                out_data->average_temperature_c = scaled;
                break;
            case UART_BMS_FIELD_MOS_TEMP:
                out_data->mosfet_temperature_valid = true;
                out_data->mosfet_temperature_c = scaled;
                break;
            case UART_BMS_FIELD_BALANCING:
                out_data->balancing_bits_valid = true;
                out_data->balancing_bits = (uint16_t)signed_value;
                break;
            case UART_BMS_FIELD_ALARM:
                out_data->alarm_bits_valid = true;
                out_data->alarm_bits = (uint16_t)signed_value;
                break;
            case UART_BMS_FIELD_WARNING:
                out_data->warning_bits_valid = true;
                out_data->warning_bits = (uint16_t)signed_value;
                break;
            case UART_BMS_FIELD_UPTIME_LOW:
                uptime_low = (uint16_t)signed_value;
                uptime_low_valid = true;
                break;
            case UART_BMS_FIELD_UPTIME_HIGH:
                uptime_high = (uint16_t)signed_value;
                uptime_high_valid = true;
                break;
            case UART_BMS_FIELD_CYCLE_LOW:
                cycle_low = (uint16_t)signed_value;
                cycle_low_valid = true;
                break;
            case UART_BMS_FIELD_CYCLE_HIGH:
                cycle_high = (uint16_t)signed_value;
                cycle_high_valid = true;
                break;
            case UART_BMS_FIELD_NONE:
            default:
                break;
        }
    }

    if (uptime_low_valid || uptime_high_valid) {
        out_data->uptime_valid = true;
        out_data->uptime_seconds = ((uint32_t)uptime_high << 16) | (uint32_t)uptime_low;
    }

    if (cycle_low_valid || cycle_high_valid) {
        out_data->cycle_count_valid = true;
        out_data->cycle_count = ((uint32_t)cycle_high << 16) | (uint32_t)cycle_low;
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

    uart_bms_publish_live_data(&data);
    return ESP_OK;
}

