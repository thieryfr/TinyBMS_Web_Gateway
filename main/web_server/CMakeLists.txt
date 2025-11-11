idf_component_register(
    SRCS
        "auth_rate_limit.c"
        "https_config.c"
        "web_server.c"
        "web_server_alerts.c"
        "web_server_api.c"
        "web_server_auth.c"
        "web_server_static.c"
        "web_server_websocket.c"
    INCLUDE_DIRS "."
    REQUIRES
        alert_manager
        system_metrics
)
