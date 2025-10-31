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

void app_main(void)
{
    event_bus_init();
    config_manager_init();
    uart_bms_init();
    can_victron_init();
    pgn_mapper_init();
    web_server_init();
    mqtt_client_init();
    monitoring_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
