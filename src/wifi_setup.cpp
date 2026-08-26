#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include "mdns_setup.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <thorlog.h>
#include <esp_wifi_config.h>
#include <esp_log.h>

#include "url_utils.h"
#include "bridge_lcd.h"
#include "jsonconfig.h"  // For config struct and global instance
#include "idf_http_server.h"
#include "http_server.h"
#include "time_sync.h"

#include "wifi_link.h"
#include "wifi_setup.h"

// Track WiFi connection state to distinguish initial connection from reconnection.
// This flag is set to true when WiFi disconnects and reset to false when reconnected.
// Used to determine appropriate LCD display: success screen (initial) vs logo (reconnection).
static bool wifi_was_disconnected = false;

/*
 * Disconnect bookkeeping.
 *
 * WIFI_CFG_EVENT_DISCONNECTED fires on every failed reconnect ATTEMPT, not once per outage,
 * so an access point that is simply gone produces an event every few seconds for as long
 * as it stays gone. The handler used to log and redraw the LCD on every one of those,
 * which is why its subscription was commented out - and with it disabled nothing ever set
 * wifi_was_disconnected, so a dropout produced no log line at all AND the reconnect branch
 * in on_wifi_got_ip() never ran, leaving mDNS unregistered after every reconnect.
 *
 * Restored, bounded to: one line when an outage starts, one more whenever the reason code
 * changes (the part actually worth seeing), otherwise at most one per interval below, and
 * one line on recovery saying how long it took and how many attempts it cost.
 */
#define WIFI_DISCONNECT_LOG_INTERVAL_MS 60000

static uint32_t wifi_disconnect_events = 0;
static uint32_t wifi_disconnect_first_ms = 0;
static uint32_t wifi_disconnect_last_log_ms = 0;
static int32_t  wifi_disconnect_last_reason = -1;

// Event callback for WiFi connecting (attempting to connect to a network)
static void on_wifi_connecting(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    if (data == nullptr) {
        return;
    }
    const char *ssid = (const char *)data;
    Log.info("WiFi connecting to %s\r\n", ssid);

    // Don't clobber the AP screen with "connecting to..." during background reconnect attempts
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK && status.ap_active) {
        return;
    }

    lcd.display_wifi_connecting_screen(ssid);
}

// Event callback for WiFi connected
static void on_wifi_connected(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    if (data == nullptr) {
        Log.warning("WiFi connected event received with invalid payload\r\n");
        return;
    }
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    Log.notice("WiFi connected to %s, channel %d, RSSI %d\r\n", info->ssid, info->channel, info->rssi);
}

// Event callback for WiFi got IP
static void on_wifi_got_ip(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    // Queued readings need a real capture time; this is the first moment SNTP can work.
    time_sync_start();

    // Closes any outage the link-health tracker has open and starts a fresh RSSI window.
    // Unconditional, and before any branch that can return early, so the tracker can never
    // be left believing an outage is still in progress while traffic is flowing.
    wifi_link_note_connected();

    // Disable modem sleep. The default WIFI_PS_MIN_MODEM only wakes the radio on DTIM
    // beacons, which inflated round trips to 30-80 ms on an otherwise clean link
    // (-52 dBm, no packet loss). Plain HTTP targets tolerate that, but a TLS handshake is
    // round-trip heavy and Google was closing the connection mid-handshake
    // (MBEDTLS_ERR_SSL_CONN_EOF) after ~19 s. Re-applied on every got-IP so it survives
    // reconnects. Costs ~20 mA, irrelevant for a mains-powered bridge.
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK)
        Log.warning("Unable to disable WiFi power save: %s\r\n", esp_err_to_name(ps_err));

    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        Log.notice("WiFi got IP: %s\r\n", status.ip);

        if (wifi_was_disconnected) {
            // This is a reconnection after a disconnect. How long it took and how many
            // attempts it cost is the whole diagnostic value of the pair of log lines.
            const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
            Log.notice("Reconnected to WiFi after %u s and %u disconnect event(s)\r\n",
                       (unsigned)((now - wifi_disconnect_first_ms) / 1000),
                       (unsigned)wifi_disconnect_events);

            // Performed on loopTask: re-registering mDNS's event handlers from a handler
            // running on this very loop leaves it with none. See mdns_setup.cpp.
            mdnsRequestReset();
            lcd.display_logo();

            wifi_was_disconnected = false;
            wifi_disconnect_events = 0;
            wifi_disconnect_last_reason = -1;
            wifi_disconnect_last_log_ms = 0;
        } else {
            // Initial connection - display success screen with access URLs
            char mdns_url[50] = "http://";
            strncat(mdns_url, config.mdnsID, 31);
            strcat(mdns_url, ".local/");

            char ip_address_url[25] = "http://";
            strncat(ip_address_url, status.ip, 16);
            strcat(ip_address_url, "/");

            lcd.display_wifi_success_screen(mdns_url, ip_address_url);
        }
    }
}

// Event callback for WiFi disconnected. Rate limited - see the counters above.
static void on_wifi_disconnected(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    if (data == nullptr) {
        Log.warning("WiFi disconnected event received with invalid payload\r\n");
        return;
    }
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    const bool firstOfOutage = !wifi_was_disconnected;

    // Self-guarding against the repeat events described above, so it does not depend on
    // firstOfOutage being right to avoid counting one outage hundreds of times.
    wifi_link_note_outage_started();

    if (firstOfOutage) {
        wifi_was_disconnected = true;
        wifi_disconnect_events = 0;
        wifi_disconnect_first_ms = now;

        // Once per outage, not once per reconnect attempt.
        lcd.display_wifi_disconnected_screen();
    }

    wifi_disconnect_events++;

    const bool reasonChanged = (int32_t)info->reason != wifi_disconnect_last_reason;
    const bool dueAgain = (now - wifi_disconnect_last_log_ms) >= WIFI_DISCONNECT_LOG_INTERVAL_MS;

    if (firstOfOutage || reasonChanged || dueAgain) {
        Log.warning("WiFi disconnected from %s, reason %d%s. Auto-reconnect in progress; "
                    "%u attempt(s) over %u s. Readings keep queueing to flash meanwhile.\r\n",
                    info->ssid, (int)info->reason,
                    (reasonChanged && !firstOfOutage) ? " (changed)" : "",
                    (unsigned)wifi_disconnect_events,
                    (unsigned)((now - wifi_disconnect_first_ms) / 1000));
        wifi_disconnect_last_log_ms = now;
    }

    wifi_disconnect_last_reason = (int32_t)info->reason;
}

// Event callback for AP started
static void on_wifi_ap_started(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    wifi_ap_status_t ap_status;
    Log.info("WiFi AP started for configuration.\r\n");
    if (wifi_cfg_get_ap_status(&ap_status) == ESP_OK) {
        Log.info("AP started: SSID: %s, IP: %s\r\n", ap_status.ssid, ap_status.ip);
        lcd.display_wifi_connect_screen(ap_status.ssid, WIFI_SETUP_AP_PASS);
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);  // Set the bandwidth of ESP32 interface
    }
}

// Event callback for provisioning stopped — initialize the HTTP server routes
static void on_provisioning_stopped(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    Log.info("WiFi provisioning stopped, initializing HTTP server.\r\n");
    http_server.init();
}

// Event callback for variable changes (e.g., mdns_name changed via WiFi config API)
static void on_var_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    if (data == nullptr) {
        return;
    }
    const wifi_var_t *var = (const wifi_var_t *)data;

    if (strcmp(var->key, "mdns_name") == 0 && strlen(var->value) > 0) {
        if (isValidHostName(var->value) && strcmp(var->value, config.mdnsID) != 0) {
            Log.notice("mDNS name changed via WiFi config: %s\r\n", var->value);
            strlcpy(config.mdnsID, var->value, sizeof(config.mdnsID));
            config.save();
            mdnsRequestReset();   // on loopTask - see mdns_setup.cpp
        }
    }
}

void disconnectWiFi() {
    Log.notice("Resetting WiFi settings via disconnectWiFi()\r\n");
    wifi_cfg_disconnect();
    wifi_cfg_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));  // Give everything a moment to settle before resetting
    esp_restart();
}


void initWiFi() {

    esp_log_level_set("wifi_cfg", ESP_LOG_VERBOSE);
    esp_log_level_set("tiltbridge", ESP_LOG_VERBOSE);

    // Initialize TCP/IP stack before starting HTTP server
    // esp_netif_init() is safe to call multiple times
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop if not already created
    esp_err_t evt_ret = esp_event_loop_create_default();
    if (evt_ret != ESP_OK && evt_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(evt_ret);  // Only fail on unexpected errors
    }

    // Start HTTP server early so we can share it with wifi_cfg
    // This prevents port conflicts when wifi_cfg's HTTP server is torn down
    esp_err_t http_ret = idf_httpd_start();
    if (http_ret != ESP_OK) {
        Log.error("Failed to start HTTP server early: %s\r\n", esp_err_to_name(http_ret));
    }

    /*
     * Subscribe to WiFi events.
     *
     * esp_wifi_config 0.2.0 moved these off esp_bus and onto ESP-IDF's default event loop,
     * so the handlers above now run on the system event task - shared with WIFI_EVENT and
     * IP_EVENT - rather than on a bus task of their own. Two consequences:
     *
     *   - Their stack is CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE, not the 4 KB esp_bus ran
     *     with. Raised to 4096 in sdkconfig.defaults to keep the budget they were written
     *     against: on_wifi_got_ip() puts a wifi_status_t and two URL buffers on the stack
     *     before it redraws the LCD.
     *   - A handler that blocks holds up IDF's own networking callbacks, so nothing here
     *     may wait on the network.
     *
     * The payload length is gone with the bus - the event id fixes the type, as it does
     * for WIFI_EVENT - so the handlers check only for NULL.
     *
     * esp_bus_sub() returned an error nobody read. A subscription that silently failed to
     * register surfaces months later as "the LCD stopped updating on reconnect", so these
     * are logged.
     */
    static const struct {
        int32_t id;
        esp_event_handler_t handler;
    } wifi_event_subs[] = {
        {WIFI_CFG_EVENT_CONNECTING,           on_wifi_connecting},
        {WIFI_CFG_EVENT_CONNECTED,            on_wifi_connected},
        {WIFI_CFG_EVENT_GOT_IP,               on_wifi_got_ip},
        {WIFI_CFG_EVENT_DISCONNECTED,         on_wifi_disconnected},
        {WIFI_CFG_EVENT_AP_START,             on_wifi_ap_started},
        {WIFI_CFG_EVENT_VAR_CHANGED,          on_var_changed},
        {WIFI_CFG_EVENT_PROVISIONING_STOPPED, on_provisioning_stopped},
    };

    for (const auto &sub : wifi_event_subs) {
        esp_err_t sub_err = esp_event_handler_register(WIFI_CFG_EVENT, sub.id, sub.handler, NULL);
        if (sub_err != ESP_OK)
            Log.error("Unable to subscribe to WiFi event %s: %s\r\n",
                      wifi_cfg_event_name((wifi_cfg_event_t)sub.id), esp_err_to_name(sub_err));
    }

    // Default variables for WiFi config - mdns_name is used to set the mDNS hostname
    // This provides a default value; if NVS has a stored value, that takes precedence
    static wifi_var_t default_vars[] = {
        {"mdns_name", "tiltbridge"},
    };

    /*
     * Configure WiFi Config, starting from the library's own defaults.
     *
     * 0.2.0 stopped patching fields left at zero - zero now means zero - and made
     * WIFI_CFG_DEFAULTS the documented starting point. That matters most for
     * `auto_reconnect`: a bool has no spare "unset" value, so a config built from scratch
     * used to disable reconnection silently. A zero retry interval is now rejected
     * outright rather than turned into a no-delay retry loop.
     *
     * The macro cannot be spliced into the aggregate initialiser it was written for: C++
     * wants designators in declaration order and forbids naming a field twice, and every
     * override below names a field the macro already sets. So take the defaults as a value
     * and assign over them. Anything not mentioned here is deliberately the library's
     * default, and a field the library adds later arrives defaulted rather than zeroed.
     *
     * The pragma is about C++, not about the macro. GCC does not raise
     * -Wmissing-field-initializers for a designated initialiser in C, but it does in C++, so
     * naming a subset of the fields - which is the entire point of WIFI_CFG_DEFAULTS - warns
     * once per field the macro does not mention, nested structs included. That is 49
     * warnings, every one of them describing a field this code leaves value-initialised on
     * purpose.
     */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    wifi_cfg_config_t wifi_config = { WIFI_CFG_DEFAULTS };
#pragma GCC diagnostic pop

    wifi_config.default_vars = default_vars;
    wifi_config.default_var_count = sizeof(default_vars) / sizeof(default_vars[0]);

    wifi_config.max_retry_per_network = 3;
    wifi_config.retry_interval_ms = 5000;
    wifi_config.retry_max_interval_ms = 60000;
    wifi_config.auto_reconnect = true;

    // If we fail to connect to any known network, start provisioning (SoftAP + captive portal)
    wifi_config.provisioning_mode = WIFI_PROV_ON_FAILURE;
    // Stop the AP and captive portal and deregister httpd endpoints once we successfully
    // connect to a WiFi network
    wifi_config.stop_provisioning_on_connect = true;
    wifi_config.provisioning_teardown_delay_ms = 5000;
    // Unregister captive portal/webui routes after provisioning so TiltBridge can register its own
    wifi_config.http_post_prov_mode = WIFI_HTTP_API_ONLY;

    // Stated in full rather than leaning on the library defaults: the captive portal's
    // address is documented and shown on the LCD, so it should not move because an
    // upstream default did.
    wifi_config.default_ap = wifi_cfg_ap_config_t{
        .ssid = WIFI_SETUP_AP_NAME,
        .password = WIFI_SETUP_AP_PASS,
        .channel = 1,
        .max_connections = 4,
        .hidden = false,
        .ip = "192.168.4.1",
        .netmask = "255.255.255.0",
        .gateway = "192.168.4.1",
        .dhcp_start = "192.168.4.2",
        .dhcp_end = "192.168.4.20",
    };
    // Ignore any saved AP config - we want to ensure the captive portal is always
    // available and consistent
    wifi_config.always_use_ap_defaults = true;
    wifi_config.enable_ap = true;

    wifi_config.http.httpd = idf_httpd_get_handle();  // Share our HTTP server with wifi_cfg
    wifi_config.http.api_base_path = "/api/wifi";
    wifi_config.http.enable_auth = false;

    // Renamed upstream from `ble` to `prov_ble` before 0.1.0. Inert unless
    // CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING is turned on.
    wifi_config.prov_ble.device_name = "TiltBridge-{id}";

    // Initialize WiFi Config
    esp_err_t err = wifi_cfg_init(&wifi_config);
    if (err != ESP_OK) {
        Log.error("Failed to initialize WiFi Config: %d\r\n", err);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // Wait for connection (5 minute timeout)
    err = wifi_cfg_wait_connected(5 * 60 * 1000);
    if (err != ESP_OK) {
        Log.error("WiFi connection timeout. Restarting device.\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // wifi_cfg handles its own provisioning teardown after the configured delay
    // (stop_provisioning_on_connect + provisioning_teardown_delay_ms)

    // Sync mDNS name FROM config TO wifi_cfg (config file is the source of truth).
    // The on_var_changed callback handles the reverse direction for real-time changes.
    wifi_cfg_set_var("mdns_name", config.mdnsID);

    initMDNS();
}

/*
 * How long wifi_cfg may insist it is disconnected, while the interface is demonstrably up
 * and holding a lease, before we force it to look again - and how long to wait before
 * trying that a second time.
 *
 * Five minutes because a brief disagreement is normal while the manager processes its own
 * reconnect; four hours is not. The cooldown exists so a manager that cannot be resynced
 * costs one short outage per quarter hour rather than a permanent one.
 */
#define WIFI_MANAGER_RESYNC_AFTER_MS    300000
#define WIFI_MANAGER_RESYNC_COOLDOWN_MS 900000

static uint32_t wifi_disagree_since_ms = 0;
static uint32_t wifi_last_resync_ms = 0;

/*
 * The link as the IP stack sees it, independent of what the WiFi manager believes.
 *
 * Shared by network_is_usable() and reconnectWiFi(), and deliberately free of the
 * disagreement counter so the latter can consult it without inflating a diagnostic.
 */
static bool sta_link_is_up() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr || !esp_netif_is_netif_up(netif))
        return false;

    esp_netif_ip_info_t ip = {};
    return esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

// Check WiFi connectivity and trigger reconnect if needed.
// Called from the main loop to ensure the manager's auto-reconnect is engaged.
//
// Throttled: loop() runs roughly every 10 ms, and the unthrottled version called
// wifi_cfg_connect() on every one of those iterations whenever the manager's flag read
// false. If that flag is ever stale-false while the link is fine, that is thousands of
// spurious connect calls per minute against the WiFi manager.
void reconnectWiFi() {
    static uint32_t lastAttemptMs = 0;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (wifi_cfg_is_connected()) {
        lastAttemptMs = 0;
        wifi_disagree_since_ms = 0;
        return;
    }

    /*
     * The manager says down. If the STA is actually associated and holds a lease, asking
     * it to connect is not merely useless, it is harmful, and it forms a loop that cannot
     * end on its own:
     *
     *   esp_wifi rejects the request - "sta is connected, disconnect before connecting to
     *   new ap" - wifi_cfg records that refusal as a failed connection, the failure keeps
     *   its state machine convinced it is disconnected, so ten seconds later it asks
     *   again. Forever.
     *
     * Observed on hardware for over an hour: ~96 flag disagreements a second, the
     * provisioning AP raised, mDNS unregistered, HTTP dropping out for 30-60 s at a time
     * and Google Sheets uploads failing with SEND_ERR_CONNECTION_FAILED - all while the
     * link itself was fine and ping ran at 0% loss.
     *
     * Trusting the interface over the manager here is the same judgement
     * network_is_usable() already makes, and for the same reason.
     */
    if (sta_link_is_up()) {
        lastAttemptMs = 0;

        if (wifi_disagree_since_ms == 0)
            wifi_disagree_since_ms = now;

        /*
         * Breaking the loop above keeps the device working, but it leaves wifi_cfg still
         * convinced it is disconnected - so mDNS is never re-registered, /api/wifi/status
         * reports an empty SSID and IP, and the provisioning AP is eventually raised.
         * Nothing in the manager resolves that by itself; it was observed wrong for over
         * four hours on hardware while every upload succeeded.
         *
         * So once it has been wrong for long enough to rule out a transient, make it look:
         * dropping the association forces its state machine to re-derive from reality, and
         * the throttled path below reconnects on the next pass.
         *
         * This deliberately breaks a working link for a few seconds, which is why it is
         * slow to trigger and rate limited. The offline queue covers the gap.
         */
        const bool longEnough =
            (now - wifi_disagree_since_ms) >= WIFI_MANAGER_RESYNC_AFTER_MS;
        const bool cooledDown =
            wifi_last_resync_ms == 0 ||
            (now - wifi_last_resync_ms) >= WIFI_MANAGER_RESYNC_COOLDOWN_MS;

        if (longEnough && cooledDown) {
            Log.warning("WiFi manager has reported disconnected for %u s while the interface is "
                        "up with a lease. Dropping the association to resynchronise it.\r\n",
                        (unsigned)((now - wifi_disagree_since_ms) / 1000));

            wifi_last_resync_ms = now;
            wifi_disagree_since_ms = 0;

            wifi_cfg_disconnect();

            // Let the stack settle before the throttled reconnect below takes over, rather
            // than racing a connect against a disconnect that has not finished.
            lastAttemptMs = now;
        }
        return;
    }

    // Genuinely down: the manager and the interface agree, so there is nothing to resync.
    wifi_disagree_since_ms = 0;

    if (lastAttemptMs != 0 && (now - lastAttemptMs) < 10000)
        return;

    lastAttemptMs = now;
    wifi_cfg_connect(NULL);
}

bool is_wifi_connected() {
    return wifi_cfg_is_connected();
}

/*
 * Desynchronisation between the WiFi manager's connected flag and the interface, measured as
 * EPISODES rather than as polls.
 *
 * The original counter incremented once per network_is_usable() call. That call sits in
 * dataSendHandler::process(), which runs on every loop() pass, so it ticked about a hundred
 * times a second while desynchronised - and a single one-minute episode read as "5,984
 * disagreements". The number was alarming, unitless and easy to mistake for thousands of
 * separate faults; it was in fact a duration in units of ten milliseconds.
 *
 * So: count how many times it happened, how long the worst one lasted, and how long in total.
 * Those answer the question the counter was there to answer.
 *
 * Touched from loopTask (via the sender) and from the sender-health monitor task, so the
 * updates are bracketed. A spinlock rather than a mutex: a handful of scalar assignments, no
 * blocking calls inside, and it must be safe to call from any task that gates work on the
 * link being usable.
 */
static portMUX_TYPE s_desync_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_desync_episodes   = 0;
static uint64_t s_desync_since_ms   = 0;   // 0 = not currently desynchronised
static uint32_t s_desync_longest_ms = 0;
static uint64_t s_desync_total_ms   = 0;

// 64-bit, for the same reason as in wifi_link.cpp: a uint32 millisecond clock wraps at 49.7
// days and this device is meant to run for months.
static uint64_t desync_millis() {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

/**
 * @brief Close the current episode, if one is open. Call when the two agree again.
 */
static void desync_end() {
    portENTER_CRITICAL(&s_desync_mux);
    if (s_desync_since_ms != 0) {
        const uint32_t held = (uint32_t)(desync_millis() - s_desync_since_ms);
        s_desync_total_ms += held;
        if (held > s_desync_longest_ms)
            s_desync_longest_ms = held;
        s_desync_since_ms = 0;
    }
    portEXIT_CRITICAL(&s_desync_mux);
}

/**
 * @brief Open an episode if one is not already open.
 */
static void desync_begin() {
    portENTER_CRITICAL(&s_desync_mux);
    if (s_desync_since_ms == 0) {
        s_desync_since_ms = desync_millis();
        s_desync_episodes++;
    }
    portEXIT_CRITICAL(&s_desync_mux);
}

WifiDesyncStats wifi_desync_stats() {
    WifiDesyncStats out{};
    const uint64_t now = desync_millis();

    portENTER_CRITICAL(&s_desync_mux);
    out.episodes  = s_desync_episodes;
    out.longestMs = s_desync_longest_ms;
    out.totalMs   = s_desync_total_ms;

    if (s_desync_since_ms != 0) {
        const uint32_t open = (uint32_t)(now - s_desync_since_ms);
        out.currentMs = open;

        // Include the episode still in progress, so the totals do not lurch when it closes.
        out.totalMs += open;
        if (open > out.longestMs)
            out.longestMs = open;
    }
    portEXIT_CRITICAL(&s_desync_mux);

    return out;
}

bool network_is_usable() {
    if (wifi_cfg_is_connected()) {
        desync_end();       // the two agree; any episode in progress is over
        return true;
    }

    // The manager says we are down. Before accepting that - and disabling every
    // outbound target as a result - check the interface itself.
    if (!sta_link_is_up()) {
        // Both say down, which is agreement, not disagreement. The link really is gone and
        // the offline queue takes over.
        desync_end();
        return false;
    }

    // Interface is up and holds a lease while the manager reports disconnected.
    // Let sends proceed; http_request() fails safely if the link really is dead.
    //
    // This is not cosmetic: it is the signature of the manager's state machine having
    // desynchronised from the driver. See reconnectWiFi().
    desync_begin();
    return true;
}

bool get_local_ip(char* ip_str, size_t len) {
    // Primary: use esp_wifi_config's status API
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK && strlen(status.ip) > 0) {
        strlcpy(ip_str, status.ip, len);
        return true;
    }

    // Fallback: use ESP-IDF netif API directly
    // This handles edge cases where wifi_cfg status might not be updated yet
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }

    ip_str[0] = '\0';
    return false;
}
