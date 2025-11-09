#include "app_config.h"

#include "event_bus.h"
#include "uart_bms.h"
#include "can_publisher.h"
#include "can_victron.h"
#include "pgn_mapper.h"
#include "web_server.h"
#include "config_manager.h"
#include "mqtt_client.h"
#include "mqtt_gateway.h"
#include "tiny_mqtt_publisher.h"
#include "monitoring.h"
#include "wifi.h"
#include "mqtt_topics.h"
#include "history_fs.h"
#include "history_logger.h"
#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "app_main";

// Application constants
#define METRICS_PUBLISH_INTERVAL_MS 1000U
#define MAIN_LOOP_DELAY_MS 1000U
#define MAIN_LOOP_WATCHDOG_INTERVAL_TICKS (30000 / MAIN_LOOP_DELAY_MS)

/**
 * @brief Configure event publishers for all modules
 *
 * @param publish_hook Function pointer to the event bus publish function
 */
static void configure_event_publishers(event_bus_publish_fn_t publish_hook)
{
    ESP_LOGI(TAG, "Configuring event publishers for all modules");

    uart_bms_set_event_publisher(publish_hook);
    can_publisher_set_event_publisher(publish_hook);
    can_victron_set_event_publisher(publish_hook);
    pgn_mapper_set_event_publisher(publish_hook);
    web_server_set_event_publisher(publish_hook);
    config_manager_set_event_publisher(publish_hook);
    mqtt_client_set_event_publisher(publish_hook);
    wifi_set_event_publisher(publish_hook);
    monitoring_set_event_publisher(publish_hook);
    tiny_mqtt_publisher_set_event_publisher(publish_hook);
    history_fs_set_event_publisher(publish_hook);
    history_logger_set_event_publisher(publish_hook);

    ESP_LOGI(TAG, "Event publishers configured successfully");
}

/**
 * @brief Initialize core infrastructure services
 *
 * Initializes configuration management, WiFi, and filesystem services.
 *
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_core_services(void)
{
    ESP_LOGI(TAG, "Initializing core services...");

    // Initialize configuration manager
    config_manager_init();
    ESP_LOGI(TAG, "  - Configuration manager initialized");

    // Initialize WiFi
    wifi_init();
    ESP_LOGI(TAG, "  - WiFi initialized");

    // Initialize history filesystem
    history_fs_init();
    ESP_LOGI(TAG, "  - History filesystem initialized");

    ESP_LOGI(TAG, "Core services initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize BMS communication services
 *
 * Initializes UART BMS, CAN Victron, CAN publisher, and PGN mapper.
 *
 * @param publish_hook Event bus publish function pointer
 * @param frame_publisher CAN frame publisher function pointer
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_bms_services(event_bus_publish_fn_t publish_hook,
                                    void (*frame_publisher)(uint32_t, const uint8_t*, uint8_t))
{
    ESP_LOGI(TAG, "Initializing BMS services...");

    if (publish_hook == NULL || frame_publisher == NULL) {
        ESP_LOGE(TAG, "Invalid function pointers provided to init_bms_services");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize UART BMS
    uart_bms_init();
    ESP_LOGI(TAG, "  - UART BMS initialized");

    // Initialize CAN Victron
    can_victron_init();
    ESP_LOGI(TAG, "  - CAN Victron initialized");

    // Initialize CAN publisher
    can_publisher_init(publish_hook, frame_publisher);
    ESP_LOGI(TAG, "  - CAN publisher initialized");

    // Initialize PGN mapper
    pgn_mapper_init();
    ESP_LOGI(TAG, "  - PGN mapper initialized");

    ESP_LOGI(TAG, "BMS services initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize networking services
 *
 * Initializes web server, MQTT client, and MQTT gateway.
 *
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_networking_services(void)
{
    ESP_LOGI(TAG, "Initializing networking services...");

    // Initialize web server
    web_server_init();
    ESP_LOGI(TAG, "  - Web server initialized");

    // Initialize MQTT client with gateway event listener
    esp_err_t ret = mqtt_client_init(mqtt_gateway_get_event_listener());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "  - MQTT client initialized");

    // Initialize MQTT gateway
    mqtt_gateway_init();
    ESP_LOGI(TAG, "  - MQTT gateway initialized");

    ESP_LOGI(TAG, "Networking services initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize monitoring and logging services
 *
 * Initializes history logger and system monitoring.
 *
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_monitoring_services(void)
{
    ESP_LOGI(TAG, "Initializing monitoring services...");

    // Initialize history logger
    history_logger_init();
    ESP_LOGI(TAG, "  - History logger initialized");

    // Initialize monitoring
    monitoring_init();
    ESP_LOGI(TAG, "  - System monitoring initialized");

    ESP_LOGI(TAG, "Monitoring services initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize MQTT metrics publisher
 *
 * Configures and initializes the tiny MQTT publisher for system metrics.
 *
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_mqtt_publisher(void)
{
    ESP_LOGI(TAG, "Initializing MQTT metrics publisher...");

    // Get MQTT configuration
    mqtt_client_config_t mqtt_cfg = {0};
    esp_err_t mqtt_err = config_manager_get_mqtt_client_config(&mqtt_cfg);

    // Configure metrics publisher with defaults
    tiny_mqtt_publisher_config_t metrics_cfg = {
        .publish_interval_ms = METRICS_PUBLISH_INTERVAL_MS,
        .qos = MQTT_TOPIC_METRICS_QOS,
        .retain = MQTT_TOPIC_METRICS_RETAIN,
    };

    // Override QoS with MQTT client configuration if available
    if (mqtt_err == ESP_OK) {
        metrics_cfg.qos = mqtt_cfg.default_qos;
        ESP_LOGI(TAG, "  - Using MQTT QoS level: %d", metrics_cfg.qos);
    } else {
        ESP_LOGW(TAG,
                 "  - MQTT configuration not available (%s), using default QoS",
                 esp_err_to_name(mqtt_err));
    }

    // Initialize publisher
    tiny_mqtt_publisher_init(&metrics_cfg);
    ESP_LOGI(TAG, "MQTT metrics publisher initialized (interval: %u ms)",
             metrics_cfg.publish_interval_ms);

    return ESP_OK;
}

/**
 * @brief Cleanup function called on initialization failure
 *
 * This function attempts to gracefully stop services that were successfully
 * initialized before a failure occurred.
 *
 * @param stage Initialization stage where failure occurred
 */
static void cleanup_on_error(const char *stage)
{
    ESP_LOGE(TAG, "Initialization failed at stage: %s", stage);
    ESP_LOGE(TAG, "Attempting cleanup of initialized resources...");

    // Note: Currently most modules don't have explicit deinit functions
    // Event bus does have event_bus_deinit(), but stopping here is safer
    // than trying to partially cleanup

    ESP_LOGE(TAG, "System cannot continue - halting");

    // Halt execution - watchdog will reset if configured
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting %s", APP_DEVICE_NAME);
    ESP_LOGI(TAG, "Version: %d.%d.%d",
             APP_VERSION_MAJOR,
             APP_VERSION_MINOR,
             APP_VERSION_PATCH);
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret;

    // Initialize event bus (must be first)
    event_bus_init();
    ESP_LOGI(TAG, "Event bus initialized");

    // Initialize status LED
    status_led_init();
    ESP_LOGI(TAG, "Status LED initialized");

    // Get event bus publish hook
    event_bus_publish_fn_t publish_hook = event_bus_get_publish_hook();
    if (publish_hook == NULL) {
        ESP_LOGE(TAG, "Failed to get event bus publish hook");
        cleanup_on_error("event_bus_publish_hook");
        return;
    }

    // Configure event publishers for all modules
    configure_event_publishers(publish_hook);

    // Initialize core services (config, wifi, filesystem)
    ret = init_core_services();
    if (ret != ESP_OK) {
        cleanup_on_error("core_services");
        return;
    }

    // Initialize MQTT publisher (depends on config)
    ret = init_mqtt_publisher();
    if (ret != ESP_OK) {
        cleanup_on_error("mqtt_publisher");
        return;
    }

    // Initialize BMS services (UART, CAN)
    ret = init_bms_services(publish_hook, can_victron_publish_frame);
    if (ret != ESP_OK) {
        cleanup_on_error("bms_services");
        return;
    }

    // Initialize networking services (web, MQTT)
    ret = init_networking_services();
    if (ret != ESP_OK) {
        cleanup_on_error("networking_services");
        return;
    }

    // Initialize monitoring services
    ret = init_monitoring_services();
    if (ret != ESP_OK) {
        cleanup_on_error("monitoring_services");
        return;
    }

    // System ready
    status_led_notify_system_ready();
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System initialization complete");
    ESP_LOGI(TAG, "All services started successfully");
    ESP_LOGI(TAG, "========================================");

    // Main loop - keep alive and log periodic heartbeat
    uint32_t loop_count = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));

        loop_count++;

        // Log heartbeat every 30 seconds
        if (loop_count % MAIN_LOOP_WATCHDOG_INTERVAL_TICKS == 0) {
            ESP_LOGI(TAG, "System running - uptime: %u seconds",
                     loop_count * MAIN_LOOP_DELAY_MS / 1000);
        }
    }
}
