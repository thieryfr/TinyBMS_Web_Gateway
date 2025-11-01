idf_component_register(SRCS "test_event_bus.c" "test_uart_bms.c" "test_end_to_end.c" "test_can_conversion.c" "test_can_publisher_integration.c" "test_mqtt_client.c" "uart_test_vectors.c"
                      INCLUDE_DIRS "."
                      REQUIRES unity event_bus uart_bms can_publisher config_manager mqtt_client)
