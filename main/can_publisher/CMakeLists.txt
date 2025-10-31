idf_component_register(SRCS "can_publisher.c" "conversion_table.c" "cvl_logic.c" "cvl_controller.c"
                      INCLUDE_DIRS "."
                      REQUIRES event_bus uart_bms can_victron)
