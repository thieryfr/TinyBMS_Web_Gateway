#include "pgn_mapper.h"

#include "esp_err.h"
#include "esp_log.h"

#include "uart_bms.h"

static event_bus_publish_fn_t s_event_publisher = NULL;
static const char *TAG = "pgn_mapper";
static uart_bms_live_data_t s_latest_bms = {0};
static bool s_has_bms = false;

static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) {
        return;
    }

    s_latest_bms = *data;
    s_has_bms = true;

    ESP_LOGD(TAG, "Received TinyBMS update: %.2f V %.2f A", data->pack_voltage_v, data->pack_current_a);
}

void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void pgn_mapper_init(void)
{
    (void)s_event_publisher;

    esp_err_t err = uart_bms_register_listener(pgn_mapper_on_bms_update, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(err));
    }

    if (!s_has_bms) {
        ESP_LOGI(TAG, "PGN mapper initialised, awaiting TinyBMS telemetry");
    }
}
