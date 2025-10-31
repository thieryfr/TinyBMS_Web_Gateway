idf_component_register(SRCS "test_event_bus.c" "test_uart_bms.c"
                      INCLUDE_DIRS "."
                      REQUIRES unity event_bus uart_bms)
