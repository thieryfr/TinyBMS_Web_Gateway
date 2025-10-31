#include "mqtt_client.h"
#include "mqtt_topics.h"

#include "esp_log.h"

static const char *TAG = "mqtt_client";

esp_err_t mqtt_client_init(void)
{
    ESP_LOGI(TAG, "MQTT client stub initialized. Publishing to %s and %s", MQTT_TOPIC_STATUS, MQTT_TOPIC_TELEMETRY);
    return ESP_OK;
}
