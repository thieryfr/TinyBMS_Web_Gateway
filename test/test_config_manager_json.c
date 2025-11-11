#include "unity.h"

#include "config_manager.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

extern void test_wifi_reset_sta_restart_count(void);
extern int test_wifi_get_sta_restart_count(void);

TEST_CASE("config_manager_snapshot_masks_secrets_and_escapes", "[config_manager]")
{
    config_manager_init();

    const mqtt_client_config_t *current = config_manager_get_mqtt_client_config();
    TEST_ASSERT_NOT_NULL(current);

    mqtt_client_config_t updated = *current;
    snprintf(updated.username, sizeof(updated.username), "user\"name\\test");
    snprintf(updated.password, sizeof(updated.password), "p@ss\\\"word");
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_mqtt_client_config(&updated));

    const char *config_payload =
        "{"
        "\"wifi\":{"
            "\"sta\":{\"ssid\":\"ssid\\\"with\\\\slashes\",\"password\":\"supersecret\",\"hostname\":\"host\"},"
            "\"ap\":{\"ssid\":\"ap\\\"name\",\"password\":\"hidden\",\"channel\":6,\"max_clients\":3}"
        "}"
        "}";

    TEST_ASSERT_EQUAL(ESP_OK,
                      config_manager_set_config_json(config_payload, strlen(config_payload)));

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t length = 0;
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_get_config_json(buffer, sizeof(buffer), &length));

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    TEST_ASSERT_NOT_NULL(wifi);
    const cJSON *sta = cJSON_GetObjectItemCaseSensitive(wifi, "sta");
    TEST_ASSERT_NOT_NULL(sta);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(sta, "ssid");
    TEST_ASSERT_TRUE(cJSON_IsString(ssid));
    TEST_ASSERT_EQUAL_STRING("ssid\"with\\slashes", ssid->valuestring);
    const cJSON *sta_password = cJSON_GetObjectItemCaseSensitive(sta, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(sta_password));
    TEST_ASSERT_EQUAL_STRING(config_manager_mask_secret("secret"), sta_password->valuestring);

    const cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    TEST_ASSERT_NOT_NULL(mqtt);
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(mqtt, "username");
    TEST_ASSERT_TRUE(cJSON_IsString(username));
    TEST_ASSERT_EQUAL_STRING("user\"name\\test", username->valuestring);
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(password));
    TEST_ASSERT_EQUAL_STRING(config_manager_mask_secret("password"), password->valuestring);

    cJSON_Delete(root);
}

TEST_CASE("config_manager_generates_secure_ap_secret_on_boot", "[config_manager][wifi]")
{
    config_manager_deinit();
    config_manager_init();

    const config_manager_wifi_settings_t *wifi = config_manager_get_wifi_settings();
    TEST_ASSERT_NOT_NULL(wifi);
    size_t password_len = strlen(wifi->ap.password);
    TEST_ASSERT_TRUE_MESSAGE(password_len >= 8, "Fallback AP password must be at least 8 characters");
}

TEST_CASE("config_manager_preserves_generated_ap_secret_when_short_password_requested", "[config_manager][wifi]")
{
    config_manager_deinit();
    config_manager_init();

    const config_manager_wifi_settings_t *initial = config_manager_get_wifi_settings();
    TEST_ASSERT_NOT_NULL(initial);

    char expected_password[CONFIG_MANAGER_WIFI_PASSWORD_MAX_LENGTH];
    strncpy(expected_password, initial->ap.password, sizeof(expected_password));
    expected_password[sizeof(expected_password) - 1] = '\0';

    const char *config_payload =
        "{\"wifi\":{\"ap\":{\"password\":\"short\"}}}";
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_config_json(config_payload, strlen(config_payload)));

    const config_manager_wifi_settings_t *updated = config_manager_get_wifi_settings();
    TEST_ASSERT_NOT_NULL(updated);
    TEST_ASSERT_EQUAL_STRING(expected_password, updated->ap.password);
}

TEST_CASE("config_manager_requests_wifi_restart_when_sta_credentials_change", "[config_manager][wifi]")
{
    config_manager_deinit();
    config_manager_init();

    test_wifi_reset_sta_restart_count();

    const char *config_payload =
        "{\"wifi\":{\"sta\":{\"ssid\":\"NewNetwork\",\"password\":\"hunter4242\"}}}";
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_config_json(config_payload, strlen(config_payload)));
    TEST_ASSERT_GREATER_THAN(0, test_wifi_get_sta_restart_count());

    test_wifi_reset_sta_restart_count();

    const char *no_change_payload =
        "{\"wifi\":{\"sta\":{\"ssid\":\"NewNetwork\"}}}";
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_config_json(no_change_payload, strlen(no_change_payload)));
    TEST_ASSERT_EQUAL(0, test_wifi_get_sta_restart_count());
}
