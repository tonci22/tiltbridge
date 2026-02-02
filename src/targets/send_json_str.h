#include <stddef.h>

#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_netif.h"

// TODO(idf_lib_swap): Replace ArduinoJson with cJSON (ESP-IDF native) or keep as header-only
#include <ArduinoJson.hpp>

// =============================================================================
// ESP-IDF INITIALIZATION REQUIREMENTS
// =============================================================================
// When fully converting to ESP-IDF, the following must be called at startup
// BEFORE using any functions in this module:
//
// #include "esp_netif.h"
// #include "esp_event.h"
// #include "esp_wifi.h"
// #include "nvs_flash.h"
//
// void init_networking() {
//     // Initialize NVS (required for WiFi)
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);
//
//     // Initialize TCP/IP network interface
//     ESP_ERROR_CHECK(esp_netif_init());
//
//     // Create default event loop (required for esp_http_client callbacks)
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//
//     // Create default WiFi station interface
//     esp_netif_create_default_wifi_sta();
//
//     // Initialize WiFi with default config
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//
//     // Configure and connect WiFi (application-specific)
//     // ...
// }
// =============================================================================




enum class sendResult {
    success,
    failure,
    retry
};

enum class httpMethod {
    HTTP_PUT,
    HTTP_POST,
    HTTP_PATCH,
    HTTP_GET,
    HTTP_DELETE
};


sendResult send_json_str(const char *payload, const char *url, httpMethod method);
sendResult send_json_str(const char *payload, const char *url, char *response, size_t response_size, httpMethod method);
