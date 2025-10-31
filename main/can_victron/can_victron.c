#include "can_victron.h"

#include "esp_log.h"

static const char *TAG = "can_victron";

esp_err_t can_victron_init(void)
{
    ESP_LOGI(TAG, "CAN Victron stub initialized");
    return ESP_OK;
}
