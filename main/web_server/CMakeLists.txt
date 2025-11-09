idf_component_register(
    SRCS
        "web_server.c"
        "web_server_alerts.c"
    INCLUDE_DIRS "."
    REQUIRES
        alert_manager
        system_metrics
)
