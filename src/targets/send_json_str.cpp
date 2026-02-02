
#include <string.h>
#include <thorlog.h>

// =============================================================================
// TODO(idf_lib_swap): ARDUINO COMPATIBILITY - REMOVE WHEN FULLY CONVERTED
// =============================================================================
// This WiFi.h include must come BEFORE esp_http_client.h to prevent
// lwip/Arduino IPAddress.h header conflicts. The conflict occurs because:
//   - esp_http_client.h -> sys/socket.h -> lwip headers define INADDR_NONE as macro
//   - Arduino's IPAddress.h tries to use INADDR_NONE as a variable name
// When fully converted to ESP-IDF, remove this include entirely.
#include <WiFi.h>
// =============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_netif.h"

#include "send_json_str.h"
#include "version.h"

// HTTP status codes
#define HTTP_CODE_OK 200
#define HTTP_CODE_NO_CONTENT 204

// I would love this to be constexpr, but that fails to compile for some reason on recent builds for the S3.
// Thanks, Espressif.
#ifndef ESP32S3
constexpr
#endif
const char* httpMethodToString(httpMethod method) {
    if (method == httpMethod::HTTP_PUT)
        return "PUT";
    else if (method == httpMethod::HTTP_POST)
        return "POST";
    else if (method == httpMethod::HTTP_PATCH)
        return "PATCH";
    else if (method == httpMethod::HTTP_DELETE)
        return "DELETE";
    else if (method == httpMethod::HTTP_GET)
        return "GET";
    return "GET";
}

static esp_http_client_method_t httpMethodToEspMethod(httpMethod method) {
    switch (method) {
        case httpMethod::HTTP_PUT:
            return HTTP_METHOD_PUT;
        case httpMethod::HTTP_POST:
            return HTTP_METHOD_POST;
        case httpMethod::HTTP_PATCH:
            return HTTP_METHOD_PATCH;
        case httpMethod::HTTP_DELETE:
            return HTTP_METHOD_DELETE;
        case httpMethod::HTTP_GET:
        default:
            return HTTP_METHOD_GET;
    }
}

static bool is_wifi_connected() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        return false;
    }
    return esp_netif_is_netif_up(netif);
}

void get_useragent(char *ua, size_t size) {
    snprintf(ua, size,
        "tiltbridge/%s (commit %s)",
        version(),
        build()
    );
}

sendResult send_json_str(const char *payload, const char *url, httpMethod method) {
    return send_json_str(payload, url, nullptr, 0, method);
}

sendResult send_json_str(const char *payload, const char *url, char *response, size_t response_size, httpMethod method) {
    char userAgent[128];
    int httpResponseCode;
    sendResult result;

    // Initialize response buffer if provided
    if (response != nullptr && response_size > 0) {
        response[0] = '\0';
    }

    if (!is_wifi_connected()) {
        Log.warning("send_json_str: Wifi not connected, skipping send.\r\n");
        return sendResult::retry;
    }

    get_useragent(userAgent, sizeof(userAgent));

    size_t payload_len = (payload != nullptr) ? strlen(payload) : 0;

    // Log the request appropriately based on whether we have a payload
    if (payload_len > 0) {
        Log.info("send_json_str: Sending %s with payload to %s\r\n", httpMethodToString(method), url);
    } else {
        Log.info("send_json_str: Sending %s to %s\r\n", httpMethodToString(method), url);
    }

    vTaskDelay(pdMS_TO_TICKS(1));  // Yield before we lock up the radio

    // Configure the HTTP client
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 6000;
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        Log.error("send_json_str: Unable to create connection\r\n");
        return sendResult::failure;
    }

    // Set the HTTP method
    esp_http_client_set_method(client, httpMethodToEspMethod(method));

    // Set headers
    if (payload_len > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    esp_http_client_set_header(client, "User-Agent", userAgent);

    // Set payload if provided
    if (payload != nullptr && payload_len > 0) {
        esp_http_client_set_post_field(client, payload, payload_len);
    }

    // Perform the request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        httpResponseCode = esp_http_client_get_status_code(client);

        // Read response into buffer
        char httpResponseBuf[2048];
        int content_length = esp_http_client_get_content_length(client);
        int total_read = 0;

        // Read the response body
        if (content_length > 0) {
            int bytes_to_read = (content_length < (int)(sizeof(httpResponseBuf) - 1)) ? content_length : (int)(sizeof(httpResponseBuf) - 1);
            int read_len;
            while (total_read < bytes_to_read) {
                read_len = esp_http_client_read(client, httpResponseBuf + total_read, bytes_to_read - total_read);
                if (read_len <= 0) {
                    break;
                }
                total_read += read_len;
            }
        }
        httpResponseBuf[total_read] = '\0';

        // Copy response to caller's buffer if provided
        if (response != nullptr && response_size > 0) {
            strlcpy(response, httpResponseBuf, response_size);
        }

        if (httpResponseCode < HTTP_CODE_OK || httpResponseCode > HTTP_CODE_NO_CONTENT) {
            Log.error("send_json_str: Send failed (%d). Response:\r\n%s\r\n",
                httpResponseCode,
                httpResponseBuf);
            result = sendResult::failure;
        } else {
            Log.verbose("send_json_str: Response:\r\n%s\r\n", httpResponseBuf);
            result = sendResult::success;
        }
    } else {
        Log.error("send_json_str: HTTP request failed: %s\r\n", esp_err_to_name(err));
        result = sendResult::failure;
    }

    esp_http_client_cleanup(client);
    return result;
}

