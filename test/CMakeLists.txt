idf_component_register(SRCS "test_event_bus.c" "test_uart_bms.c" "test_end_to_end.c" "uart_test_vectors.c"
                      INCLUDE_DIRS "."
                      REQUIRES unity event_bus uart_bms config_manager)
