#include "pgn_mapper.h"

#include "esp_err.h"
#include "esp_log.h"

#include "uart_bms.h"

static const char *TAG = "pgn_mapper";

static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_live_data_t s_latest_data = {0};
static bool s_has_latest_data = false;

static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) {
        return;
    }

    if (!s_has_latest_data) {
        ESP_LOGI(TAG, "Received TinyBMS telemetry sample");
    }

    s_latest_data = *data;
    s_has_latest_data = true;

    (void)s_event_publisher;
    // Future work: translate ::s_latest_data into CAN-bus PGNs and publish them
}

void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;
}

void pgn_mapper_init(void)
{
    esp_err_t err = uart_bms_register_listener(pgn_mapper_on_bms_update, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to register TinyBMS listener: %s", esp_err_to_name(err));
    }
}
