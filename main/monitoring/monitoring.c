#include "monitoring.h"

#include "esp_log.h"

static const char *TAG = "monitoring";

esp_err_t monitoring_init(void)
{
    ESP_LOGI(TAG, "Monitoring stub initialized");
    return ESP_OK;
}
