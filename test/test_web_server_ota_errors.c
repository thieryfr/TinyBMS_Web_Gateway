#include "unity.h"

#include "web_server_ota_errors.h"
#include "cJSON.h"

TEST_CASE("ota_error_mapping_invalid_boundary", "[web_server][ota]")
{
    TEST_ASSERT_EQUAL_STRING("error", web_server_ota_status_string(WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY));
    TEST_ASSERT_EQUAL(400, web_server_ota_http_status(WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY));
    TEST_ASSERT_EQUAL_STRING("Multipart boundary is invalid or unsupported",
                              web_server_ota_error_message(WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY));

    cJSON *root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(web_server_ota_set_response_fields(root, WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY, NULL));

    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    TEST_ASSERT_TRUE(cJSON_IsString(status));
    TEST_ASSERT_EQUAL_STRING("error", status->valuestring);

    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    TEST_ASSERT_TRUE(cJSON_IsNumber(code));
    TEST_ASSERT_EQUAL((int)WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY, code->valueint);

    cJSON_Delete(root);
}

TEST_CASE("ota_error_mapping_unsupported_content_type", "[web_server][ota]")
{
    TEST_ASSERT_EQUAL(415, web_server_ota_http_status(WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE));
    TEST_ASSERT_EQUAL_STRING("Unsupported firmware content type",
                              web_server_ota_error_message(WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE));

    cJSON *root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(web_server_ota_set_response_fields(root,
                                                        WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE,
                                                        "Custom message"));

    const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    TEST_ASSERT_TRUE(cJSON_IsString(message));
    TEST_ASSERT_EQUAL_STRING("Custom message", message->valuestring);

    cJSON_Delete(root);
}
