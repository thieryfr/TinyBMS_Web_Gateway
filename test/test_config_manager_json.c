#include "unity.h"

#include "config_manager.h"
#include "cJSON.h"

#include <stdio.h>

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
