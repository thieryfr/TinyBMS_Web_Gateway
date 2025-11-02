#include "app_config.h"

#include "event_bus.h"
#include "uart_bms.h"
#include "can_publisher.h"
#include "can_victron.h"
#include "pgn_mapper.h"
#include "web_server.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "mqtt_gateway.h"
#include "tiny_mqtt_publisher.h"
#include "monitoring.h"
#include "wifi.h"
#include "mqtt_topics.h"
#include "history_fs.h"
#include "history_logger.h"
#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    event_bus_init();
    status_led_init();
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
    tiny_mqtt_publisher_set_event_publisher(publish_hook);
    history_fs_set_event_publisher(publish_hook);
    history_logger_set_event_publisher(publish_hook);

    config_manager_init();
    const mqtt_client_config_t *mqtt_cfg = config_manager_get_mqtt_client_config();
    tiny_mqtt_publisher_config_t metrics_cfg = {
        .publish_interval_ms = 1000U,
        .qos = MQTT_TOPIC_METRICS_QOS,
        .retain = MQTT_TOPIC_METRICS_RETAIN,
    };
    if (mqtt_cfg != NULL) {
        metrics_cfg.qos = mqtt_cfg->default_qos;
    }
    tiny_mqtt_publisher_init(&metrics_cfg);
    wifi_init();
    history_fs_init();
    uart_bms_init();
    can_victron_init();
    can_publisher_init(publish_hook, can_victron_publish_frame);
    pgn_mapper_init();
    web_server_init();
    mqtt_client_init(mqtt_gateway_get_event_listener());
    mqtt_gateway_init();
    history_logger_init();
    monitoring_init();

    status_led_notify_system_ready();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
