#include "unity.h"

#include "web_server.h"
#include "config_manager.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static void prepare_known_config(const char *wifi_password, const char *mqtt_password)
{
    config_manager_deinit();
    config_manager_init();

    const mqtt_client_config_t *current = config_manager_get_mqtt_client_config();
    TEST_ASSERT_NOT_NULL(current);

    mqtt_client_config_t updated = *current;
    if (mqtt_password != NULL) {
        snprintf(updated.password, sizeof(updated.password), "%s", mqtt_password);
        TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_mqtt_client_config(&updated));
    }

    if (wifi_password != NULL) {
        char payload[128];
        int written = snprintf(payload,
                               sizeof(payload),
                               "{\"wifi\":{\"sta\":{\"password\":\"%s\"}}}",
                               wifi_password);
        TEST_ASSERT_GREATER_THAN(0, written);
        TEST_ASSERT_LESS_THAN((int)sizeof(payload), written);
        TEST_ASSERT_EQUAL(ESP_OK, config_manager_set_config_json(payload, (size_t)written));
    }
}

TEST_CASE("web_server_public_snapshot_masks_secrets", "[web_server][config]")
{
    const char *wifi_password = "httppass123";
    const char *mqtt_password = "httpsecret";
    prepare_known_config(wifi_password, mqtt_password);

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t length = 0;
    const char *visibility = NULL;
    TEST_ASSERT_EQUAL(ESP_OK,
                      web_server_prepare_config_snapshot("/api/config?include_secrets=1",
                                                         false,
                                                         buffer,
                                                         sizeof(buffer),
                                                         &length,
                                                         &visibility));
    TEST_ASSERT_NOT_NULL(visibility);
    TEST_ASSERT_EQUAL_STRING("public", visibility);

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    TEST_ASSERT_NOT_NULL(wifi);
    const cJSON *sta = cJSON_GetObjectItemCaseSensitive(wifi, "sta");
    TEST_ASSERT_NOT_NULL(sta);
    const cJSON *sta_password = cJSON_GetObjectItemCaseSensitive(sta, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(sta_password));
    TEST_ASSERT_EQUAL_STRING(CONFIG_MANAGER_SECRET_MASK, sta_password->valuestring);

    const cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    TEST_ASSERT_NOT_NULL(mqtt);
    const cJSON *mqtt_password_json = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(mqtt_password_json));
    TEST_ASSERT_EQUAL_STRING(CONFIG_MANAGER_SECRET_MASK, mqtt_password_json->valuestring);

    cJSON_Delete(root);
}

TEST_CASE("web_server_full_snapshot_requires_authorization", "[web_server][config]")
{
    const char *wifi_password = "httppass123";
    const char *mqtt_password = "httpsecret";
    prepare_known_config(wifi_password, mqtt_password);

    char buffer[CONFIG_MANAGER_MAX_CONFIG_SIZE];
    size_t length = 0;
    const char *visibility = NULL;
    TEST_ASSERT_EQUAL(ESP_OK,
                      web_server_prepare_config_snapshot("/api/config?include_secrets=true",
                                                         true,
                                                         buffer,
                                                         sizeof(buffer),
                                                         &length,
                                                         &visibility));
    TEST_ASSERT_NOT_NULL(visibility);
    TEST_ASSERT_EQUAL_STRING("full", visibility);

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    TEST_ASSERT_NOT_NULL(wifi);
    const cJSON *sta = cJSON_GetObjectItemCaseSensitive(wifi, "sta");
    TEST_ASSERT_NOT_NULL(sta);
    const cJSON *sta_password = cJSON_GetObjectItemCaseSensitive(sta, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(sta_password));
    TEST_ASSERT_EQUAL_STRING(wifi_password, sta_password->valuestring);

    const cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    TEST_ASSERT_NOT_NULL(mqtt);
    const cJSON *mqtt_password_json = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
    TEST_ASSERT_TRUE(cJSON_IsString(mqtt_password_json));
    TEST_ASSERT_EQUAL_STRING(mqtt_password, mqtt_password_json->valuestring);

    cJSON_Delete(root);
}
