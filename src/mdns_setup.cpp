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

/*
 * Deferred reset, for callers that run on the system event loop task.
 *
 * mdnsReset() tears mDNS down and brings it back, and mdns_free()/mdns_init() unregister
 * and re-register the mDNS component's own esp_event handlers. Called from a handler
 * running on that same loop, ESP-IDF cannot remove them inline: the loop task already
 * holds loop->mutex, so esp_event_handler_unregister_with_internal()'s
 * `xSemaphoreTake(mutex, 0)` fails and it takes the deferred path, which only marks the
 * node `unregistered` and queues a cleanup event. mdns_init()'s re-register then finds
 * that still-present node, logs "handler already registered, overwriting", updates only
 * its argument - crucially without clearing `unregistered` - and the queued cleanup then
 * deletes the node outright. mDNS is left with no event handlers at all.
 *
 * Observed on hardware as three "handler already registered, overwriting" warnings during
 * the first reconnect after the esp_wifi_config 0.2.0 event migration, which is what moved
 * these callers onto the event loop task in the first place - under esp_bus they ran on a
 * task of their own and removal was inline.
 *
 * Deferring to loopTask also keeps mdnsReset()'s failure path - a one second delay and a
 * restart - off the loop that carries IDF's own networking callbacks.
 */
static volatile bool s_reset_pending = false;

void mdnsRequestReset() {
    s_reset_pending = true;
}

void mdnsServicePendingReset() {
    if (!s_reset_pending)
        return;

    s_reset_pending = false;
    mdnsReset();
}
