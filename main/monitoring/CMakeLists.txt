idf_component_register(SRCS "monitoring.c"
                      INCLUDE_DIRS "." "../include"
                      REQUIRES event_bus uart_bms can_publisher
                      PRIV_REQUIRES esp_timer esp_common)
