/**
 * @file idf_http_server.h
 * @brief ESP-IDF HTTP server abstraction
 *
 * This replaces ESPAsyncWebServer with native esp-idf http_server.
 * Every handler runs synchronously on the httpd task.
 */

#ifndef IDF_HTTP_SERVER_H
#define IDF_HTTP_SERVER_H

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

// Configuration
#define HTTP_SERVER_PORT            80
#define HTTP_MAX_URI_LEN            512
#define HTTP_MAX_RESP_SIZE          4096

/*
 * This is a share of CONFIG_LWIP_MAX_SOCKETS (10), not a private budget - whatever the
 * web server holds is unavailable to the outbound TLS client, SNTP, mDNS and DHCP.
 *
 * It was 7 because the removed async worker pool implied HTTP_MAX_ASYNC_REQUESTS + 2,
 * which left only 3 for everything else. A browser holding several parallel connections
 * to the UI could then starve an upload, which fails immediately and distinctively:
 *
 *   esp-tls: Failed to create socket (family 2 socktype 1 protocol 0)
 *   HTTP_CLIENT: Connection failed, sock < 0
 *
 * 4 leaves 6 for the rest of the system and still lets a browser pipeline the UI's
 * assets; lru_purge_enable recycles the oldest socket rather than refusing a new one,
 * so this bounds concurrency, not the number of users.
 */
#define HTTP_MAX_OPEN_SOCKETS       4

/**
 * @brief Initialize and start the HTTP server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t idf_httpd_start(void);

/**
 * @brief Stop the HTTP server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t idf_httpd_stop(void);

/**
 * @brief Get the HTTP server handle
 * @return httpd_handle_t or NULL if not started
 */
httpd_handle_t idf_httpd_get_handle(void);

/**
 * @brief Register a URI handler with the server
 * @param uri_handler Pointer to httpd_uri_t structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t idf_httpd_register_uri(const httpd_uri_t *uri_handler);

/**
 * @brief Check if server is running
 * @return true if server is active
 */
bool idf_httpd_is_running(void);

#endif // IDF_HTTP_SERVER_H
