idf_component_register(SRCS "system_metrics.c"
                      INCLUDE_DIRS "."
                      REQUIRES event_bus cjson freertos)
