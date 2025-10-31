idf_component_register(SRCS "config_manager.c"
                      INCLUDE_DIRS "."
                      REQUIRES event_bus uart_bms nvs_flash)
