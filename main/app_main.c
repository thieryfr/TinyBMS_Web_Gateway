#include "app_config.h"

#include "event_bus.h"
#include "uart_bms.h"
#include "can_publisher.h"
#include "can_victron.h"
#include "pgn_mapper.h"
#include "web_server.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "monitoring.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    event_bus_init();
    event_bus_publish_fn_t publish_hook = event_bus_get_publish_hook();
    uart_bms_set_event_publisher(publish_hook);
    can_publisher_set_event_publisher(publish_hook);
    can_victron_set_event_publisher(publish_hook);
    pgn_mapper_set_event_publisher(publish_hook);
    web_server_set_event_publisher(publish_hook);
    config_manager_set_event_publisher(publish_hook);
    mqtt_client_set_event_publisher(publish_hook);
    wifi_set_event_publisher(publish_hook);
    monitoring_set_event_publisher(publish_hook);

    config_manager_init();
    wifi_init();
    uart_bms_init();
    can_victron_init();
    can_publisher_init(publish_hook, can_victron_publish_frame);
    pgn_mapper_init();
    web_server_init();
    mqtt_client_init(NULL);
    monitoring_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
