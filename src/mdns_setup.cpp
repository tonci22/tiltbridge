#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <mdns.h>
#include <esp_wifi_config.h>

#include <thorlog.h>

#include "jsonconfig.h"
#include "wifi_setup.h"  // For WEB_SERVER_PORT
#include "mdns_setup.h"
#include "version.h"

static void registerMdnsServices() {
    mdns_service_add(NULL, "_http", "_tcp", WEB_SERVER_PORT, NULL, 0);

    mdns_txt_item_t tiltbridge_txt[] = {
        {(char *)"build",    (char *)build()},
        {(char *)"branch",   (char *)branch()},
        {(char *)"version",  (char *)version()},
        {(char *)"hardware", (char *)hardware()},
    };
    mdns_service_add(NULL, "_tiltbridge", "_tcp", WEB_SERVER_PORT,
                     tiltbridge_txt,
                     sizeof(tiltbridge_txt) / sizeof(tiltbridge_txt[0]));
}

void initMDNS() {
    if (mdns_init() != ESP_OK || mdns_hostname_set(config.mdnsID) != ESP_OK) {
        Log.error("Error setting up MDNS responder.\r\n");
    } else {
        Log.notice("mDNS responder started, hostname: %s.local\r\n", config.mdnsID);
    }

    registerMdnsServices();

    // Store the mDNS name in wifi_cfg's custom variables for persistence
    wifi_cfg_set_var("mdns_name", config.mdnsID);
}

void mdnsReset() {
    mdns_free();
    if (mdns_init() != ESP_OK || mdns_hostname_set(config.mdnsID) != ESP_OK) {
        Log.error("Error resetting MDNS responder.");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        Log.notice("mDNS responder restarted, hostname: %s.local.\r\n", config.mdnsID);
        registerMdnsServices();
    }

    // Update the mDNS name in wifi_cfg's custom variables
    wifi_cfg_set_var("mdns_name", config.mdnsID);
}
