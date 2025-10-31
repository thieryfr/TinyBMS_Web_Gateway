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

#define UART_BMS_LISTENER_SLOTS 4
#define UART_BMS_EVENT_BUFFERS 2
#define UART_BMS_MAX_FRAME_SIZE 128
#define UART_BMS_RX_BUFFER_SIZE 256
#define UART_BMS_TASK_STACK 4096
#define UART_BMS_TASK_PRIORITY 12
#define UART_BMS_POLL_TIMEOUT_MS 200

#ifdef ESP_PLATFORM
#define UART_BMS_DEFAULT_PORT ((int)UART_NUM_1)
#define UART_BMS_DEFAULT_TX_PIN ((int)GPIO_NUM_17)
#define UART_BMS_DEFAULT_RX_PIN ((int)GPIO_NUM_16)
#else
#define UART_BMS_DEFAULT_PORT (1)
#define UART_BMS_DEFAULT_TX_PIN (17)
#define UART_BMS_DEFAULT_RX_PIN (16)
#endif

#define UART_BMS_DEFAULT_BAUDRATE 115200
#define UART_BMS_DEFAULT_POLL_INTERVAL_MS 1000

static const char *TAG = "uart_bms";

static const uint16_t s_poll_addresses[UART_BMS_MAX_REGISTERS] = {
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028,
    0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F, 0x0030, 0x0031,
    0x0032, 0x0033, 0x0034, 0x0066, 0x0067, 0x0071, 0x0072, 0x0131, 0x0132,
    0x0133, 0x013B, 0x013C, 0x013D, 0x013E, 0x013F, 0x01F4, 0x01F5, 0x01F6,
    0x01F7, 0x01F8, 0x01F9,
};

static const uint8_t s_poll_request[] = {
    0xAA, 0x09, 0x4E, 0x20, 0x00, 0x21, 0x00, 0x22, 0x00, 0x23, 0x00, 0x24, 0x00,
    0x25, 0x00, 0x26, 0x00, 0x27, 0x00, 0x28, 0x00, 0x29, 0x00, 0x2A, 0x00,
    0x2B, 0x00, 0x2C, 0x00, 0x2D, 0x00, 0x2E, 0x00, 0x2F, 0x00, 0x30, 0x00,
    0x31, 0x00, 0x32, 0x00, 0x33, 0x00, 0x34, 0x00, 0x66, 0x00, 0x67, 0x00,
    0x71, 0x00, 0x72, 0x00, 0x31, 0x01, 0x32, 0x01, 0x33, 0x01, 0x3B, 0x01,
    0x3C, 0x01, 0x3D, 0x01, 0x3E, 0x01, 0x3F, 0x01, 0xF4, 0x01, 0xF5, 0x01,
    0xF6, 0x01, 0xF7, 0x01, 0xF8, 0x01, 0xF9, 0x01, 0xBB, 0x55,
};

typedef enum {
    UART_BMS_FIELD_NONE = 0,
    UART_BMS_FIELD_PACK_VOLTAGE,
    UART_BMS_FIELD_PACK_CURRENT,
    UART_BMS_FIELD_MIN_CELL_MV,
    UART_BMS_FIELD_MAX_CELL_MV,
    UART_BMS_FIELD_SOC_PERCENT,
    UART_BMS_FIELD_SOH_PERCENT,
    UART_BMS_FIELD_AVG_TEMP,
    UART_BMS_FIELD_MOS_TEMP,
    UART_BMS_FIELD_BALANCING_BITS,
    UART_BMS_FIELD_ALARM_BITS,
    UART_BMS_FIELD_WARNING_BITS,
    UART_BMS_FIELD_UPTIME_LOW,
    UART_BMS_FIELD_UPTIME_HIGH,
    UART_BMS_FIELD_CYCLE_LOW,
    UART_BMS_FIELD_CYCLE_HIGH,
} uart_bms_field_t;

typedef struct {
    uint16_t address;
    bool is_signed;
    float scale;
    uart_bms_field_t field;
} uart_bms_register_descriptor_t;

static const uart_bms_register_descriptor_t s_register_descriptors[] = {
    {0x0020, false, 0.01f, UART_BMS_FIELD_PACK_VOLTAGE},
    {0x0021, true, 0.1f, UART_BMS_FIELD_PACK_CURRENT},
    {0x0022, false, 1.0f, UART_BMS_FIELD_MIN_CELL_MV},
    {0x0023, false, 1.0f, UART_BMS_FIELD_MAX_CELL_MV},
    {0x0024, false, 0.01f, UART_BMS_FIELD_SOC_PERCENT},
    {0x0025, false, 0.01f, UART_BMS_FIELD_SOH_PERCENT},
    {0x0026, true, 0.1f, UART_BMS_FIELD_AVG_TEMP},
    {0x0027, true, 0.1f, UART_BMS_FIELD_MOS_TEMP},
    {0x0028, false, 1.0f, UART_BMS_FIELD_BALANCING_BITS},
    {0x0029, false, 1.0f, UART_BMS_FIELD_ALARM_BITS},
    {0x002A, false, 1.0f, UART_BMS_FIELD_WARNING_BITS},
    {0x0066, false, 1.0f, UART_BMS_FIELD_UPTIME_LOW},
    {0x0067, false, 1.0f, UART_BMS_FIELD_UPTIME_HIGH},
    {0x0071, false, 1.0f, UART_BMS_FIELD_CYCLE_LOW},
    {0x0072, false, 1.0f, UART_BMS_FIELD_CYCLE_HIGH},
};

typedef struct {
    uart_bms_data_callback_t callback;
    void *context;
} uart_bms_listener_t;

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_listener_t s_listeners[UART_BMS_LISTENER_SLOTS];
static uart_bms_live_data_t s_event_buffers[UART_BMS_EVENT_BUFFERS];
static size_t s_next_event_buffer = 0;
static uart_bms_config_t s_active_config;
static bool s_config_set = false;
static bool s_uart_initialised = false;
static TaskHandle_t s_uart_task_handle = NULL;
static uint8_t s_rx_buffer[UART_BMS_MAX_FRAME_SIZE];
static size_t s_rx_length = 0;

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
    for (size_t i = 0; i < sizeof(s_register_descriptors) / sizeof(s_register_descriptors[0]); ++i) {
        if (s_register_descriptors[i].address == address) {
            return &s_register_descriptors[i];
        }
    }
    return NULL;
}

static uart_bms_config_t uart_bms_default_config(void)
{
    uart_bms_config_t config = {
        .uart_port = UART_BMS_DEFAULT_PORT,
        .tx_pin = UART_BMS_DEFAULT_TX_PIN,
        .rx_pin = UART_BMS_DEFAULT_RX_PIN,
        .baud_rate = UART_BMS_DEFAULT_BAUDRATE,
        .poll_interval = pdMS_TO_TICKS(UART_BMS_DEFAULT_POLL_INTERVAL_MS),
    };
    return config;
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
    if (s_event_publisher == NULL) {
        uart_bms_notify_listeners(data);
        return;
    }

    uart_bms_live_data_t *storage = &s_event_buffers[s_next_event_buffer];
    s_next_event_buffer = (s_next_event_buffer + 1) % UART_BMS_EVENT_BUFFERS;
    *storage = *data;

    event_bus_event_t event = {
        .id = UART_BMS_EVENT_ID_LIVE_DATA,
        .payload = storage,
        .payload_size = sizeof(*storage),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish TinyBMS live data event");
    }

    uart_bms_notify_listeners(storage);
}

static void uart_bms_reset_buffer(void)
{
    s_rx_length = 0;
}

static void uart_bms_consume_bytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (s_rx_length >= sizeof(s_rx_buffer)) {
            ESP_LOGW(TAG, "RX buffer overflow, dropping byte");
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
                ESP_LOGW(TAG, "Invalid TinyBMS frame length %zu", total_len);
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
                ESP_LOGW(TAG, "Discarding TinyBMS frame (%d)", (int)err);
                memmove(s_rx_buffer, s_rx_buffer + 1, s_rx_length - 1);
                --s_rx_length;
                progress = (s_rx_length > 0);
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
                                       sizeof(s_poll_request));
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
            } else {
                break;
            }
        }

        vTaskDelay(poll_interval);
    }
}
#endif

void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void uart_bms_apply_config(const uart_bms_config_t *config)
{
    if (config == NULL) {
        s_active_config = uart_bms_default_config();
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
        ESP_LOGE(TAG, "Failed to configure UART (%d)", (int)err);
        return;
    }

    err = uart_set_pin((uart_port_t)s_active_config.uart_port,
                       s_active_config.tx_pin,
                       s_active_config.rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART pins (%d)", (int)err);
        return;
    }

    err = uart_driver_install((uart_port_t)s_active_config.uart_port,
                              UART_BMS_RX_BUFFER_SIZE,
                              0,
                              0,
                              NULL,
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver (%d)", (int)err);
        return;
    }

    if (xTaskCreate(uart_bms_task,
                    "uart_bms_rx",
                    UART_BMS_TASK_STACK,
                    NULL,
                    UART_BMS_TASK_PRIORITY,
                    &s_uart_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create TinyBMS task");
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

    if (frame[0] != 0xAA || frame[1] != 0x09) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t payload_len = frame[2];
    size_t expected_len = payload_len + 5;
    if ((payload_len % 2) != 0 || length < expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t crc_expected = (uint16_t)frame[expected_len - 2] | (uint16_t)(frame[expected_len - 1] << 8);
    uint16_t crc_computed = uart_bms_compute_crc(frame, expected_len - 2);
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

        if (i < (sizeof(s_poll_addresses) / sizeof(s_poll_addresses[0]))) {
            uint16_t address = s_poll_addresses[i];
            out_data->registers[i].address = address;
            out_data->registers[i].raw_value = raw_value;

            const uart_bms_register_descriptor_t *descriptor = find_descriptor(address);
            if (descriptor != NULL) {
                int32_t signed_value = descriptor->is_signed ? (int32_t)(int16_t)raw_value
                                                             : (int32_t)raw_value;
                float scaled_value = (float)signed_value * descriptor->scale;

                switch (descriptor->field) {
                    case UART_BMS_FIELD_PACK_VOLTAGE:
                        out_data->pack_voltage_v = scaled_value;
                        break;
                    case UART_BMS_FIELD_PACK_CURRENT:
                        out_data->pack_current_a = scaled_value;
                        break;
                    case UART_BMS_FIELD_MIN_CELL_MV:
                        out_data->min_cell_mv = (uint16_t)signed_value;
                        break;
                    case UART_BMS_FIELD_MAX_CELL_MV:
                        out_data->max_cell_mv = (uint16_t)signed_value;
                        break;
                    case UART_BMS_FIELD_SOC_PERCENT:
                        out_data->state_of_charge_pct = scaled_value;
                        break;
                    case UART_BMS_FIELD_SOH_PERCENT:
                        out_data->state_of_health_pct = scaled_value;
                        break;
                    case UART_BMS_FIELD_AVG_TEMP:
                        out_data->average_temperature_c = scaled_value;
                        break;
                    case UART_BMS_FIELD_MOS_TEMP:
                        out_data->mosfet_temperature_c = scaled_value;
                        break;
                    case UART_BMS_FIELD_BALANCING_BITS:
                        out_data->balancing_bits = (uint16_t)signed_value;
                        break;
                    case UART_BMS_FIELD_ALARM_BITS:
                        out_data->alarm_bits = (uint16_t)signed_value;
                        break;
                    case UART_BMS_FIELD_WARNING_BITS:
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
        } else {
            out_data->registers[i].address = 0;
            out_data->registers[i].raw_value = raw_value;
        }
    }

    if (uptime_low_valid || uptime_high_valid) {
        out_data->uptime_seconds = ((uint32_t)uptime_high << 16) | (uint32_t)uptime_low;
    }

    if (cycle_low_valid || cycle_high_valid) {
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

