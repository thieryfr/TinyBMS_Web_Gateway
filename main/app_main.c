#include "app_config.h"
#include "event_bus.h"
#include "uart_bms.h"
#include "can_victron.h"
#include "pgn_mapper.h"
#include "web_server.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "monitoring.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "TinyBMS Web Gateway skeleton initialized");

    (void)event_bus_init();
    (void)uart_bms_init();
    (void)can_victron_init();
    (void)pgn_mapper_init();
    (void)web_server_init();
    (void)config_manager_init();
    (void)mqtt_client_init();
    (void)monitoring_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
