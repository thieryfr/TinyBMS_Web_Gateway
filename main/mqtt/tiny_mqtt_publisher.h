#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_bus.h"
#include "uart_bms.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t publish_interval_ms;
    int qos;
    bool retain;
} tiny_mqtt_publisher_config_t;

#define TINY_MQTT_PUBLISH_INTERVAL_KEEP UINT32_MAX
#define TINY_MQTT_MAX_PAYLOAD_SIZE      1024U

typedef struct {
    const char *payload;
    size_t payload_length;
    int qos;
    bool retain;
} tiny_mqtt_publisher_message_t;

void tiny_mqtt_publisher_set_event_publisher(event_bus_publish_fn_t publisher);
void tiny_mqtt_publisher_init(const tiny_mqtt_publisher_config_t *config);
void tiny_mqtt_publisher_deinit(void);
void tiny_mqtt_publisher_apply_config(const tiny_mqtt_publisher_config_t *config);
void tiny_mqtt_publisher_reset(void);
void tiny_mqtt_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context);

#ifdef __cplusplus
}
#endif

