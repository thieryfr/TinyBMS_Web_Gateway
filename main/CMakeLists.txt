idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus uart_bms can_victron pgn_mapper web_server config_manager mqtt_client monitoring
)
