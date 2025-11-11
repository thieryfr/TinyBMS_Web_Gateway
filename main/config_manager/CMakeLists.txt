idf_component_register(
    SRCS
        "config_manager_core.c"
        "config_manager_json.c"
        "config_manager_mqtt.c"
        "config_manager_network.c"
        "config_manager_validation.c"
    INCLUDE_DIRS "."
    REQUIRES event_bus uart_bms nvs_flash spiffs cjson
)
