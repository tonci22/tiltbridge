/**
 * @file idf_static_files.cpp
 * @brief Static file serving implementation
 */

#include "idf_static_files.h"
#include "idf_http_server.h"

#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "filesystem.h"

static const char *TAG = "idf_static";

// Content type mappings
typedef struct {
    const char *extension;
    const char *content_type;
} content_type_map_t;

static const content_type_map_t content_types[] = {
    {".htm", "text/html"},
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".xml", "text/xml"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {NULL, NULL}
};

const char *idf_static_get_content_type(const char *filename) {
    if (filename == NULL) {
        return "text/plain";
    }

    // Find the last dot in the filename
    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
        return "text/plain";
    }

    // Search content type mappings
    for (int i = 0; content_types[i].extension != NULL; i++) {
        if (strcasecmp(dot, content_types[i].extension) == 0) {
            return content_types[i].content_type;
        }
    }

    return "text/plain";
}

/**
 * @brief Check if a file exists in the filesystem
 */
static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t idf_static_serve_file(httpd_req_t *req, const char *file_path) {
    char full_path[STATIC_MAX_PATH_LEN];
    char gz_path[STATIC_MAX_PATH_LEN + 4]; // +4 for ".gz" suffix
    bool use_gzip = false;

    // Build full filesystem path
    snprintf(full_path, sizeof(full_path), "%s/%s", FILESYSTEM_PREFIX, file_path);
    snprintf(gz_path, sizeof(gz_path), "%s.gz", full_path);

    ESP_LOGD(TAG, "Serving file: %s", full_path);

    // Check for gzipped version first
    if (file_exists(gz_path)) {
        use_gzip = true;
        strncpy(full_path, gz_path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
        ESP_LOGD(TAG, "Using gzipped version: %s", full_path);
    } else if (!file_exists(full_path)) {
        ESP_LOGD(TAG, "File not found: %s", full_path);
        return ESP_FAIL;
    }

    // Open file
    FILE *file = fopen(full_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        return ESP_FAIL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Determine content type from original file path (not gz path)
    const char *content_type = idf_static_get_content_type(file_path);
    httpd_resp_set_type(req, content_type);

    // Set gzip encoding if applicable
    if (use_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    /*
     * Three tiers, and they have to agree with tiltbridge_web_ui/vite.config.js.
     *
     * no-store - /conf/ and any HTML.
     *
     *   Everything under /conf/ is live device state: the calibration point files the web UI
     *   re-reads immediately after changing them. Serving those with max-age meant the browser
     *   answered the reload from cache and showed the OLD contents, so a calibration point
     *   that had genuinely been deleted stayed on screen and the delete button looked dead.
     *
     *   HTML is index.html, which every SPA route hands back. It names the hashed asset files,
     *   so a cached copy of it is a copy naming assets that an uploadfs may have deleted. This
     *   was the "why is the help page empty" bug: the previous build wrote unhashed names, a
     *   browser kept the OLD chunks under the NEW build's names, a chunk the new build no
     *   longer emitted 404'd, and the lazy route's dynamic import() rejected leaving a blank
     *   page with nothing in the console. index.html must always come from the device.
     *
     * immutable - assets/, which Vite writes with a content hash in every filename.
     *
     *   A given URL there can only ever have one body, so it is safe to cache for a year and
     *   there is no revalidation to pay on a device this slow. A new build is a new filename,
     *   picked up by the freshly-fetched index.html.
     *
     * max-age - everything else: the favicons and site.webmanifest at the root, and the
     *   esp_wifi_config UI under wifiui/. None of those carry a hash, so they keep the short
     *   window rather than being pinned for a year.
     */
    char cache_control[64];

    // file_path arrives without a leading slash here (the separator is added when full_path
    // is built), but tolerate one so this cannot silently stop matching.
    const char *rel = file_path;
    while (*rel == '/')
        rel++;

    const bool is_config = (strncmp(rel, "conf/", 5) == 0);

    const char *dot = strrchr(rel, '.');
    const bool is_html = (dot != NULL) &&
                         (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0);

    const bool is_hashed_asset = (strncmp(rel, "assets/", 7) == 0);

    if (is_config || is_html) {
        strlcpy(cache_control, "no-store", sizeof(cache_control));
    } else if (is_hashed_asset) {
        strlcpy(cache_control, "max-age=31536000, immutable", sizeof(cache_control));
    } else {
        snprintf(cache_control, sizeof(cache_control), "max-age=%d", STATIC_CACHE_MAX_AGE);
    }

    httpd_resp_set_hdr(req, "Cache-Control", cache_control);

    // Stream file in chunks
    char *chunk = (char *)malloc(STATIC_FILE_CHUNK_SIZE);
    if (chunk == NULL) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate chunk buffer");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
    }

    size_t bytes_sent = 0;
    while (bytes_sent < file_size) {
        size_t to_read = (file_size - bytes_sent < STATIC_FILE_CHUNK_SIZE) ?
                         (file_size - bytes_sent) : STATIC_FILE_CHUNK_SIZE;

        size_t read = fread(chunk, 1, to_read, file);
        if (read == 0) {
            break;
        }

        esp_err_t err = httpd_resp_send_chunk(req, chunk, read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk");
            free(chunk);
            fclose(file);
            return err;
        }

        bytes_sent += read;
    }

    // Send empty chunk to signal end
    httpd_resp_send_chunk(req, NULL, 0);

    free(chunk);
    fclose(file);

    ESP_LOGD(TAG, "Served %d bytes for %s", bytes_sent, file_path);
    return ESP_OK;
}

esp_err_t idf_static_handler(httpd_req_t *req) {
    const char *uri = req->uri;

    // Skip leading slash
    if (uri[0] == '/') {
        uri++;
    }

    // Default to index.html for root
    if (strlen(uri) == 0 || strcmp(uri, "") == 0) {
        uri = "index.html";
    }

    // Try to serve the file
    esp_err_t ret = idf_static_serve_file(req, uri);
    if (ret != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    return ESP_OK;
}

esp_err_t idf_static_spa_handler(httpd_req_t *req) {
    // Always serve index.html for SPA routes
    return idf_static_serve_file(req, "index.html");
}

esp_err_t idf_static_not_found_handler(httpd_req_t *req) {
    const char *uri = req->uri;

    // Skip leading slash
    if (uri[0] == '/') {
        uri++;
    }

    // Try to serve the file
    if (idf_static_serve_file(req, uri) == ESP_OK) {
        return ESP_OK;
    }

    // File not found, return 404
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "Not Found");
}

/**
 * @brief Handler for explicit 404 page request
 */
static esp_err_t idf_static_404_page_handler(httpd_req_t *req) {
    return idf_static_serve_file(req, "404.htm");
}

esp_err_t idf_static_register_handlers(void) {
    // Register root handler
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = idf_static_spa_handler,
        .user_ctx = NULL
    };
    esp_err_t ret = idf_httpd_register_uri(&root_uri);
    if (ret != ESP_OK) {
        return ret;
    }

    // Register index.html explicitly
    httpd_uri_t index_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = idf_static_spa_handler,
        .user_ctx = NULL
    };
    ret = idf_httpd_register_uri(&index_uri);
    if (ret != ESP_OK) {
        return ret;
    }

    // Register 404 page
    httpd_uri_t notfound_uri = {
        .uri = "/404/",
        .method = HTTP_GET,
        .handler = idf_static_404_page_handler,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&notfound_uri);

    ESP_LOGI(TAG, "Registered static file handlers");
    return ESP_OK;
}

esp_err_t idf_static_register_catchall(void) {
    // Register wildcard catch-all handler for static files
    // This MUST be called LAST after all other handlers are registered
    // so that specific routes take precedence
    httpd_uri_t catchall_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = idf_static_handler,
        .user_ctx = NULL
    };
    esp_err_t ret = idf_httpd_register_uri(&catchall_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register catch-all handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Registered static file catch-all handler");
    return ESP_OK;
}

esp_err_t idf_static_register_spa_routes(void) {
    // Vue.js routes that should all serve index.html
    static const char *vue_routes[] = {
        "/config/",
        "/config/tiltbridge/",
        "/config/queue/",
        "/target/",
        "/target/fermentrack/",
        "/target/legacy_fermentrack/",
        "/target/gsheets/",
        "/target/brewersfriend/",
        "/target/brewfather/",
        "/target/grainfather/",
        "/target/brewstatus/",
        "/target/taplistio/",
        "/target/mqtt/",
        "/target/generic/",
        "/target/influxdb/",
        "/help/",
        "/about/",
        // Wildcard: the Vue route is /calibrate/:color/, so there is no fixed path to match.
        // Registered before the "/*" catch-all (http_server.cpp calls spa_routes first), and
        // esp_http_server matches handlers in registration order, so this wins.
        "/calibrate/*",
        NULL
    };

    for (int i = 0; vue_routes[i] != NULL; i++) {
        httpd_uri_t uri = {
            .uri = vue_routes[i],
            .method = HTTP_GET,
            .handler = idf_static_spa_handler,
            .user_ctx = NULL
        };
        esp_err_t ret = idf_httpd_register_uri(&uri);
        if (ret != ESP_OK && ret != ESP_ERR_HTTPD_HANDLER_EXISTS) {
            ESP_LOGW(TAG, "Failed to register SPA route %s: %s", vue_routes[i], esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Registered SPA routes");
    return ESP_OK;
}
