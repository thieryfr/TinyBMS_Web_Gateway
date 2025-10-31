idf_component_register(SRCS "pgn_mapper.c"
                      INCLUDE_DIRS "." "../include"
                      REQUIRES event_bus uart_bms
                      PRIV_REQUIRES esp_common)
