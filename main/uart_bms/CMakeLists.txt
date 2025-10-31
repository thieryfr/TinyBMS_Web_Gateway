idf_component_register(SRCS "uart_bms.c" "uart_bms_protocol.c"
                      INCLUDE_DIRS "." "../include"
                      REQUIRES event_bus
                      PRIV_REQUIRES driver esp_timer esp_common freertos)
