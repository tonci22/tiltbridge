#include <cstdio>
#include <cstring>
#include <ctime>

#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <thorlog.h>

#include "time_sync.h"

// Anything earlier than 2025-01-01 means the clock has not really been set, so a bogus
// or partial sync cannot mark records as validly timestamped.
#define MIN_PLAUSIBLE_UNIX_TIME 1735689600UL

static bool s_initialised = false;
static bool s_started = false;

static void on_time_synced(struct timeval *tv) {
    (void)tv;
    Log.notice("SNTP time synchronised: %lu\r\n", (unsigned long)time(nullptr));
}

void time_sync_init() {
    if (s_initialised)
        return;

    // Everything internal is UTC; setting TZ explicitly keeps gmtime_r and localtime_r
    // in agreement rather than depending on the default environment.
    setenv("TZ", "UTC0", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = false;                  // started from the got-IP handler
    cfg.sync_cb = on_time_synced;
    cfg.server_from_dhcp = false;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Log.error("Unable to initialise SNTP: %s\r\n", esp_err_to_name(err));
        return;
    }

    s_initialised = true;
}

void time_sync_start() {
    if (!s_initialised || s_started)
        return;

    if (esp_netif_sntp_start() == ESP_OK) {
        s_started = true;
        Log.info("SNTP started.\r\n");
    }
}

bool time_is_valid() {
    return (uint32_t)time(nullptr) >= MIN_PLAUSIBLE_UNIX_TIME;
}

uint32_t utc_now() {
    const uint32_t now = (uint32_t)time(nullptr);
    return (now >= MIN_PLAUSIBLE_UNIX_TIME) ? now : 0;
}

void format_utc_iso8601(uint32_t unixSeconds, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return;

    out[0] = '\0';
    if (unixSeconds == 0)
        return;

    const time_t t = (time_t)unixSeconds;
    struct tm tm_utc;
    if (gmtime_r(&t, &tm_utc) == nullptr)
        return;

    strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}
