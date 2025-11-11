#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA upload response codes used by the web server API.
 */
typedef enum {
    WEB_SERVER_OTA_OK = 0,
    WEB_SERVER_OTA_ERROR_EMPTY_PAYLOAD = 1,
    WEB_SERVER_OTA_ERROR_MISSING_CONTENT_TYPE = 2,
    WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY = 3,
    WEB_SERVER_OTA_ERROR_SUBSYSTEM_BUSY = 4,
    WEB_SERVER_OTA_ERROR_MALFORMED_MULTIPART = 5,
    WEB_SERVER_OTA_ERROR_STREAM_FAILURE = 6,
    WEB_SERVER_OTA_ERROR_MISSING_FIRMWARE_FIELD = 7,
    WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE = 8,
    WEB_SERVER_OTA_ERROR_VALIDATION_FAILED = 9,
    WEB_SERVER_OTA_ERROR_ENCODING_FAILED = 10,
} web_server_ota_error_code_t;

/**
 * @brief Return the default status string associated with an OTA response code.
 */
static inline const char *web_server_ota_status_string(web_server_ota_error_code_t code)
{
    return (code == WEB_SERVER_OTA_OK) ? "ok" : "error";
}

/**
 * @brief Map OTA response code to a default human readable message.
 */
static inline const char *web_server_ota_error_message(web_server_ota_error_code_t code)
{
    switch (code) {
    case WEB_SERVER_OTA_OK:
        return "Firmware uploaded successfully";
    case WEB_SERVER_OTA_ERROR_EMPTY_PAYLOAD:
        return "OTA payload is empty";
    case WEB_SERVER_OTA_ERROR_MISSING_CONTENT_TYPE:
        return "Content-Type header is missing";
    case WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY:
        return "Multipart boundary is invalid or unsupported";
    case WEB_SERVER_OTA_ERROR_SUBSYSTEM_BUSY:
        return "OTA subsystem is busy";
    case WEB_SERVER_OTA_ERROR_MALFORMED_MULTIPART:
        return "Malformed multipart payload";
    case WEB_SERVER_OTA_ERROR_STREAM_FAILURE:
        return "Failed to stream OTA payload";
    case WEB_SERVER_OTA_ERROR_MISSING_FIRMWARE_FIELD:
        return "Multipart field must be named 'firmware'";
    case WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE:
        return "Unsupported firmware content type";
    case WEB_SERVER_OTA_ERROR_VALIDATION_FAILED:
        return "OTA validation failed";
    case WEB_SERVER_OTA_ERROR_ENCODING_FAILED:
        return "Failed to encode OTA response";
    default:
        return "Unknown OTA error";
    }
}

/**
 * @brief Default HTTP status code associated with an OTA response code.
 */
static inline int web_server_ota_http_status(web_server_ota_error_code_t code)
{
    switch (code) {
    case WEB_SERVER_OTA_OK:
        return 200;
    case WEB_SERVER_OTA_ERROR_EMPTY_PAYLOAD:
    case WEB_SERVER_OTA_ERROR_MISSING_CONTENT_TYPE:
    case WEB_SERVER_OTA_ERROR_INVALID_BOUNDARY:
    case WEB_SERVER_OTA_ERROR_MALFORMED_MULTIPART:
    case WEB_SERVER_OTA_ERROR_MISSING_FIRMWARE_FIELD:
        return 400;
    case WEB_SERVER_OTA_ERROR_UNSUPPORTED_CONTENT_TYPE:
        return 415;
    case WEB_SERVER_OTA_ERROR_SUBSYSTEM_BUSY:
        return 503;
    case WEB_SERVER_OTA_ERROR_STREAM_FAILURE:
    case WEB_SERVER_OTA_ERROR_VALIDATION_FAILED:
    case WEB_SERVER_OTA_ERROR_ENCODING_FAILED:
        return 500;
    default:
        return 500;
    }
}

/**
 * @brief Populate a JSON object with the standard OTA response fields.
 *
 * The object must be newly created (no existing fields with the same names).
 *
 * @param object JSON object to populate.
 * @param code   OTA response code.
 * @param message_override Optional message string; if NULL the default message is used.
 * @return true on success, false on allocation failure or invalid parameters.
 */
static inline bool web_server_ota_set_response_fields(cJSON *object,
                                                      web_server_ota_error_code_t code,
                                                      const char *message_override)
{
    if (object == NULL) {
        return false;
    }

    const char *status = web_server_ota_status_string(code);
    const char *message = (message_override != NULL) ? message_override : web_server_ota_error_message(code);

    if (!cJSON_AddStringToObject(object, "status", status)) {
        return false;
    }
    if (!cJSON_AddNumberToObject(object, "error_code", (int)code)) {
        return false;
    }
    if (!cJSON_AddStringToObject(object, "message", message)) {
        return false;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_OTA_ERRORS_H
