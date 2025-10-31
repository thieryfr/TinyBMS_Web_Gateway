idf_component_register(SRCS "uart_bms.cpp" "uart_response_parser.cpp" "uart_bms_protocol.c" "uart_frame_builder.cpp"
                      INCLUDE_DIRS "." "../include" "../../docs"
                      REQUIRES event_bus
                      PRIV_REQUIRES driver esp_timer esp_common freertos)
