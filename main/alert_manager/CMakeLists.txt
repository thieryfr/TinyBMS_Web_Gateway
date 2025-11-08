idf_component_register(
    SRCS "alert_manager.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash json
    REQUIRES freertos esp_common uart_bms event_bus
)
