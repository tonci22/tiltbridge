#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <esp_wifi_config.h>
#include <thorlog.h>

#include "rssi_stats.h"
#include "jsonconfig.h"
#include "sender_health.h"
#include "wifi_setup.h"

#include "wifi_link.h"

/*
 * How often to sample, and how long a window of samples describes.
 *
 * The window matters more than it looks. "Min/max since boot" sounds more useful than
 * "min/max over the last hour" and is in fact useless: over three weeks it degenerates into
 * the single worst glitch ever seen, which tells you nothing about the link you have now.
 * An hour is long enough to catch an access point that drifts and short enough to still
 * describe the present.
 *
 * It also keeps RssiStats::samples clear of its uint16_t saturation cap by three orders of
 * magnitude - 360 samples per window against a 65535 ceiling. The Tilt path deliberately
 * relies on that cap saturating; here it can simply never be reached, so the average never
 * silently stops moving.
 */
#define WIFI_LINK_SAMPLE_INTERVAL_MS 10000
#define WIFI_LINK_WINDOW_MS          3600000

/*
 * Timestamps are 64-bit milliseconds, not the uint32_t used elsewhere in this codebase.
 * A uint32_t millisecond counter wraps at 49.7 days, and these feed subtractions that are
 * displayed to the user ("connected for", "last outage"). This device is meant to sit on a
 * fermenter for months, so the wrap is reachable in normal operation rather than
 * theoretical. esp_timer_get_time() is already int64 - narrowing it would be the only way
 * to introduce the bug.
 */
static uint64_t link_millis() {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

// Why the last access-point move ended the way it did. Reported as-is to the UI.
enum class RoamResult : uint8_t {
    NONE = 0,           // never attempted
    LANDED,             // associated with the intended access point
    NO_CANDIDATE,       // scanned; nothing on this SSID was materially stronger
    SCAN_FAILED,        // esp_wifi_scan_start() refused
    OUT_OF_MEMORY,      // could not allocate the scan result buffer
    SAME_AP,            // reconnected, but to the radio we were trying to leave
    REASSOC_TIMEOUT,    // still down when the budget ran out; manager still retrying
};

static const char *roamResultName(RoamResult r) {
    switch (r) {
        case RoamResult::LANDED:           return "LANDED";
        case RoamResult::NO_CANDIDATE:     return "NO_CANDIDATE";
        case RoamResult::SCAN_FAILED:      return "SCAN_FAILED";
        case RoamResult::OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
        case RoamResult::SAME_AP:          return "SAME_AP";
        case RoamResult::REASSOC_TIMEOUT:  return "REASSOC_TIMEOUT";
        default:                           return "NONE";
    }
}

/*
 * What the roam logic is doing right now, so the UI can say why the device is busy.
 *
 * Distinct from roamInProgress below, which exists only to suppress the dropout counter for
 * the deliberate disconnect. This covers the whole operation including the scan, and must not
 * widen that suppression: a genuine outage during the scan should still count as one.
 */
enum class RoamPhase : uint8_t { IDLE = 0, SCANNING, RECONNECTING };

static const char *roamPhaseName(RoamPhase p) {
    switch (p) {
        case RoamPhase::SCANNING:     return "SCANNING";
        case RoamPhase::RECONNECTING: return "RECONNECTING";
        default:                      return "IDLE";
    }
}

struct WifiLinkState {
    RssiStats rssi{};
    uint64_t  windowStartedMs   = 0;
    uint64_t  connectedSinceMs  = 0;  // 0 = the link is not currently up
    uint64_t  outageStartedMs   = 0;  // 0 = no outage in progress
    uint64_t  lastOutageEndedMs = 0;  // 0 = no outage has ended since boot
    uint32_t  lastOutageMs      = 0;
    uint32_t  outages           = 0;  // distinct outages, not reconnect attempts

    /*
     * Which access point we are actually attached to, and how often that has changed.
     *
     * This is the field that makes the difference on a repeated network. A repeater serves
     * the same SSID as the router's own access point, so `ssid` cannot tell them apart -
     * and RSSI to a repeater two metres away reads EXCELLENT no matter how bad the
     * repeater's own backhaul is. The BSSID is the only thing here that says which radio
     * you are talking to, and a climbing `roams` count is what a device flapping between
     * the two looks like from the inside.
     */
    uint8_t   bssid[6]          = {0};
    bool      hasBssid          = false;
    uint32_t  roams             = 0;

    // Re-associations this code forced because the link was parked on a poor access
    // point. Counted apart from `roams` and `outages` so a self-inflicted drop is never
    // mistaken for the network misbehaving.
    uint32_t  roamRecoveries    = 0;
    bool      roamInProgress    = false;

    /*
     * Attempts and the last outcome, not just successes.
     *
     * The first version of this counted only `roamRecoveries`, and suppressed the outage
     * counter for the duration of an attempt - so an attempt that failed left no trace
     * anywhere. A device then sat on a -89 dBm radio for 3.16 hours with a -56 dBm one
     * available and the telemetry could not say whether the recovery had run and failed or
     * never run at all. Anything that can fail silently will.
     */
    RoamPhase roamPhase         = RoamPhase::IDLE;
    uint32_t  roamAttempts      = 0;
    uint64_t  roamLastAttemptMs = 0;  // 0 = never attempted
    RoamResult roamLastResult   = RoamResult::NONE;
};

static WifiLinkState s_link;

/*
 * Written from loopTask and the WiFi event task, read from the httpd task.
 *
 * A spinlock rather than a mutex: every holder does nothing but copy a few dozen bytes of
 * plain scalars, so it is held for a handful of instructions, it cannot fail, and it is
 * callable from the event task without a blocking-call hazard. Nothing that takes a lock of
 * its own - esp_wifi_sta_get_ap_info() in particular - is ever called inside it.
 *
 * Without it a reader can see a torn set: samples incremented before sum, giving a wrong
 * average for one poll. Cosmetic rather than dangerous, but this file exists to be trusted.
 */
static portMUX_TYPE s_link_mux = portMUX_INITIALIZER_UNLOCKED;

void wifi_link_sample() {
    static uint64_t lastSampleMs = 0;
    const uint64_t now = link_millis();

    if (lastSampleMs != 0 && (now - lastSampleMs) < WIFI_LINK_SAMPLE_INTERVAL_MS)
        return;

    lastSampleMs = now;

    // Deliberately outside the critical section below - this takes the driver's own locks.
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
        return;  // Not associated. Nothing to sample, and no sample is not a sample of zero.

    portENTER_CRITICAL(&s_link_mux);

    /*
     * A change of BSSID means a different radio, so the window's min and max stop
     * describing the link we are on and are restarted - the same reasoning as on reconnect.
     *
     * Counted here rather than on the disconnect path because a roam does not reliably
     * produce one: the driver can move between access points on the same SSID without the
     * link ever going down, in which case `outages` stays at zero and this is the only
     * evidence it happened.
     */
    const bool changedAp = s_link.hasBssid && memcmp(s_link.bssid, ap.bssid, 6) != 0;
    if (changedAp)
        s_link.roams++;

    memcpy(s_link.bssid, ap.bssid, 6);
    s_link.hasBssid = true;

    if (changedAp || s_link.windowStartedMs == 0 ||
        (now - s_link.windowStartedMs) >= WIFI_LINK_WINDOW_MS) {
        s_link.rssi = RssiStats{};
        s_link.windowStartedMs = now;
    }
    s_link.rssi.add(ap.rssi);
    portEXIT_CRITICAL(&s_link_mux);
}

void wifi_link_note_outage_started() {
    const uint64_t now = link_millis();

    portENTER_CRITICAL(&s_link_mux);

    /*
     * A drop we caused ourselves is not a dropout. Without this, every recovery would
     * inflate the figure the user reads as "how unstable is my WiFi" - and would do it in
     * exactly the situation where they are already worried about instability.
     */
    if (s_link.roamInProgress) {
        s_link.connectedSinceMs = 0;
        portEXIT_CRITICAL(&s_link_mux);
        return;
    }

    // Idempotent, because the disconnected event fires on every failed reconnect ATTEMPT.
    // Counting those would report an access point that is simply switched off as hundreds
    // of separate outages.
    if (s_link.outageStartedMs == 0) {
        s_link.outageStartedMs = now;
        s_link.outages++;
    }
    s_link.connectedSinceMs = 0;
    portEXIT_CRITICAL(&s_link_mux);
}

void wifi_link_note_connected() {
    const uint64_t now = link_millis();

    portENTER_CRITICAL(&s_link_mux);
    if (s_link.outageStartedMs != 0) {
        s_link.lastOutageMs      = (uint32_t)(now - s_link.outageStartedMs);
        s_link.lastOutageEndedMs = now;
        s_link.outageStartedMs   = 0;
    }
    s_link.connectedSinceMs = now;

    /*
     * Start a fresh window. A new association can be a different access point, a different
     * band or a different channel, so the previous window's min and max no longer describe
     * the link we are actually on. Cleared outright rather than rebased on the stale
     * `latest`, so the aggregates read as absent until a real sample lands - the reported
     * current signal comes from the driver, so nothing goes blank in the meantime.
     */
    s_link.rssi = RssiStats{};
    s_link.windowStartedMs = now;
    portEXIT_CRITICAL(&s_link_mux);
}

void wifi_link_json(JsonObject o) {
    WifiLinkState snap;
    portENTER_CRITICAL(&s_link_mux);
    snap = s_link;
    portEXIT_CRITICAL(&s_link_mux);

    const uint64_t now = link_millis();

    wifi_ap_record_t ap;
    const bool associated = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);

    /*
     * Both truths, side by side, deliberately.
     *
     * `associated` is the driver: are we on an access point right now. `managerConnected`
     * is what wifi_cfg believes. They are supposed to agree; this fork exists because they
     * demonstrably do not, and a user looking at a healthy signal while uploads fail needs
     * to be able to see which of the two is lying.
     */
    o["associated"] = associated;
    o["managerConnected"] = is_wifi_connected();

    /*
     * How often, and for how long, the manager's flag has disagreed with a demonstrably-up
     * interface - the desynchronisation described in reconnectWiFi(). Read here, never
     * updated, which is why this function calls is_wifi_connected() and not
     * network_is_usable(): observing a diagnostic must not alter it.
     */
    const WifiDesyncStats desync = wifi_desync_stats();
    o["desyncEpisodes"]  = desync.episodes;
    o["desyncLongestSec"] = (uint32_t)(desync.longestMs / 1000);
    o["desyncTotalSec"]   = (uint32_t)(desync.totalMs / 1000);

    if (desync.currentMs > 0)
        o["desyncCurrentSec"] = (uint32_t)(desync.currentMs / 1000);
    else
        o["desyncCurrentSec"] = nullptr;

    if (associated) {
        // The live value, not the last sample, so the headline figure is never up to
        // WIFI_LINK_SAMPLE_INTERVAL_MS stale.
        o["rssiLatest"]  = ap.rssi;
        o["rssiQuality"] = rssiQualityName(ap.rssi);
        o["ssid"]        = (const char *)ap.ssid;
        o["channel"]     = ap.primary;

        // Copied into the document by ArduinoJson 7, as with `ip` below.
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        o["bssid"] = (const char *)bssid;
    } else {
        o["rssiLatest"]  = nullptr;
        o["rssiQuality"] = nullptr;
        o["ssid"]        = nullptr;
        o["channel"]     = nullptr;
        o["bssid"]       = nullptr;
    }

    // How many times the access point changed under us. On a repeated network this
    // separates "my signal is fine" from "I keep being handed between two radios".
    o["roams"] = snap.roams;
    o["roamRecoveries"] = snap.roamRecoveries;

    /*
     * Attempts and the last outcome, so a recovery that runs and fails is visible. Without
     * these a failed attempt left no trace at all - see the note on the state fields.
     */
    o["roamPhase"] = roamPhaseName(snap.roamPhase);
    o["roamAttempts"] = snap.roamAttempts;
    o["roamLastResult"] = roamResultName(snap.roamLastResult);

    if (snap.roamLastAttemptMs != 0)
        o["roamLastAttemptAgoSec"] = (uint32_t)((now - snap.roamLastAttemptMs) / 1000);
    else
        o["roamLastAttemptAgoSec"] = nullptr;

    if (snap.rssi.hasData) {
        o["rssiAverage"] = snap.rssi.average();
        o["rssiMinimum"] = snap.rssi.minimum;
        o["rssiMaximum"] = snap.rssi.maximum;
        o["rssiSamples"] = snap.rssi.samples;
        o["windowSec"]   = (uint32_t)((now - snap.windowStartedMs) / 1000);
    } else {
        o["rssiAverage"] = nullptr;
        o["rssiMinimum"] = nullptr;
        o["rssiMaximum"] = nullptr;
        o["rssiSamples"] = 0;
        o["windowSec"]   = 0;
    }

    /*
     * Safe despite the buffer being a local: ArduinoJson 7 copies a const char* into the
     * document's own pool and stores by reference only for string literals. (This differs
     * from v6, where the assignment below would have left a dangling pointer once this
     * function returned - the document is serialized by the caller, after we are gone.)
     */
    char ip[16] = "";
    if (get_local_ip(ip, sizeof(ip)) && ip[0] != '\0')
        o["ip"] = (const char *)ip;
    else
        o["ip"] = nullptr;

    // --- stability: the half of "is the WiFi good?" that signal strength cannot answer ---

    o["outages"] = snap.outages;

    if (snap.connectedSinceMs != 0)
        o["connectedForSec"] = (uint32_t)((now - snap.connectedSinceMs) / 1000);
    else
        o["connectedForSec"] = nullptr;

    if (snap.outageStartedMs != 0)
        o["currentOutageSec"] = (uint32_t)((now - snap.outageStartedMs) / 1000);
    else
        o["currentOutageSec"] = nullptr;

    if (snap.lastOutageEndedMs != 0) {
        o["lastOutageAgoSec"] = (uint32_t)((now - snap.lastOutageEndedMs) / 1000);
        // Rounded, so a sub-second blip reads as 1 s rather than as no time at all.
        o["lastOutageDurationSec"] = (uint32_t)((snap.lastOutageMs + 500) / 1000);
    } else {
        o["lastOutageAgoSec"] = nullptr;
        o["lastOutageDurationSec"] = nullptr;
    }
}

/*
 * ---------------------------------------------------------------------------------------
 * Recovering from a good signal to the wrong access point.
 *
 * The component builds its station config as `wifi_config_t cfg = {0}` and fills in only
 * the SSID and password (esp_wifi_config_network.c:90-92). Every remaining field therefore
 * takes the ESP-IDF zero default, and two of those defaults decide this device's fate on a
 * network with a repeater:
 *
 *   scan_method    = WIFI_FAST_SCAN            "scan will end after find SSID match AP"
 *   threshold.rssi = 0                         no minimum signal at all
 *
 * So association is not "pick the best access point", it is "take the first one heard and
 * stop looking" - at any signal strength. A repeater and the router's own radio share the
 * SSID, so which one it lands on is decided by scan order, not by quality.
 *
 * Worse, nothing ever revisits the decision. btm_enabled, mbo_enabled and rm_enabled are
 * all 0 and no roaming support is compiled in, ESP-IDF does no background steering of its
 * own, the component only reconnects in response to a disconnect, and reconnectWiFi() only
 * acts when the link is already down. Once parked on the distant radio at -85 dBm the
 * device stays there until that link itself fails - indefinitely, even after the nearby
 * repeater comes back.
 *
 * This closes that hole: when the window average has been poor for long enough to rule out
 * a passing fade, scan, and if a materially stronger radio on the same SSID exists, force a
 * re-association onto it.
 *
 * It deliberately breaks a working link for a few seconds, which is why it is slow to
 * trigger and rate limited to once an hour. That is the same trade reconnectWiFi() already
 * makes when it drops the association to resynchronise the manager, and the offline queue
 * covers the gap either way.
 * ---------------------------------------------------------------------------------------
 */

#define WIFI_ROAM_POOR_RSSI_DBM       (-78)     // window average at or below this is poor
#define WIFI_ROAM_POOR_SUSTAIN_MS     600000   // ...and must stay poor for ten minutes
#define WIFI_ROAM_MIN_GAIN_DBM        12       // a candidate must beat us by this much
#define WIFI_ROAM_COOLDOWN_MS         3600000  // at most one attempt an hour
#define WIFI_ROAM_MIN_SAMPLES         30      // ~5 min of samples before the average counts
#define WIFI_ROAM_MAX_SCAN_RECORDS    24
/*
 * How long to wait for the manager to bring the link back after we stand down.
 *
 * It does not even try for the first retry_interval_ms (5 s as configured in initWiFi), then
 * has to scan every channel - ALL_CHANNEL_SCAN, per the component patch - associate and
 * complete DHCP. 25 s covers that with room to spare, and the earlier 10 s budget was simply
 * too short, which is one of the two reasons the first version of this failed silently.
 *
 * Worst case blocking on loopTask is the scan (~4 s) plus this, about 29 s, against the 60 s
 * floor on senderStaleRebootSec - so it cannot provoke a sender recovery reboot.
 */
#define WIFI_ROAM_REASSOC_TIMEOUT_MS  25000

/**
 * @brief Record how an attempt ended. Every exit path after the guards must call this.
 */
static void wifi_roam_set_phase(RoamPhase p) {
    portENTER_CRITICAL(&s_link_mux);
    s_link.roamPhase = p;
    portEXIT_CRITICAL(&s_link_mux);
}

static void wifi_roam_note_result(RoamResult r) {
    portENTER_CRITICAL(&s_link_mux);
    s_link.roamLastResult = r;
    if (r == RoamResult::LANDED)
        s_link.roamRecoveries++;

    // Every exit path after the guards records a result, so clearing the phase here means no
    // path can leave the UI showing a scan that finished.
    s_link.roamPhase = RoamPhase::IDLE;
    portEXIT_CRITICAL(&s_link_mux);
}

void wifi_link_check_ap() {
    // loopTask only, so these need no locking; only the counters below are shared.
    static uint64_t poorSinceMs = 0;
    static uint64_t lastAttemptMs = 0;

    /*
     * Runtime switch, checked first and on every pass, so turning it off in the web UI stops
     * the behaviour immediately rather than at the next reboot. Deliberately does NOT reset
     * poorSinceMs: re-enabling it mid-outage should not restart the ten minute clock from
     * zero when the link has demonstrably been poor for longer than that already.
     */
    if (!config.wifiRoamEnabled)
        return;

    const uint64_t now = link_millis();

    // Only ever act on a link that is genuinely up. If it is down, reconnectWiFi() owns the
    // problem and a scan would collide with the connect sequence it is driving.
    if (!is_wifi_connected()) {
        poorSinceMs = 0;
        return;
    }

    int8_t   avg;
    uint16_t samples;
    portENTER_CRITICAL(&s_link_mux);
    avg     = s_link.rssi.average();
    samples = s_link.rssi.samples;
    portEXIT_CRITICAL(&s_link_mux);

    // A handful of samples is not a verdict. The window also restarts on reconnect and once
    // an hour, so this guard is what stops a fresh window's first reading from deciding.
    if (samples < WIFI_ROAM_MIN_SAMPLES)
        return;

    if (avg > WIFI_ROAM_POOR_RSSI_DBM) {
        poorSinceMs = 0;
        return;
    }

    if (poorSinceMs == 0) {
        poorSinceMs = now;
        return;
    }

    if ((now - poorSinceMs) < WIFI_ROAM_POOR_SUSTAIN_MS)
        return;

    if (lastAttemptMs != 0 && (now - lastAttemptMs) < WIFI_ROAM_COOLDOWN_MS)
        return;

    /*
     * Never mid-upload. Tearing the link down under an in-flight request would abort it and
     * hand the sender a failure it would rightly count against the target.
     */
    if (sender_health.isHeld())
        return;

    /*
     * Only now ask the driver where we are.
     *
     * This call used to sit at the top of the function, above every one of the cheap gates
     * above - so on a healthy link it ran on every loop() pass, about a hundred times a
     * second, taking the WiFi driver's lock each time to answer a question whose answer was
     * then thrown away. Everything above reads our own counters and a couple of statics;
     * the driver is consulted once, at the point where we are actually about to act on it.
     */
    wifi_ap_record_t cur;
    if (esp_wifi_sta_get_ap_info(&cur) != ESP_OK) {
        // Associated a moment ago by the manager's reckoning, but not now. Nothing to
        // compare against, so stand down and let reconnectWiFi() own it.
        poorSinceMs = 0;
        return;
    }

    const unsigned poorMinutes = (unsigned)((now - poorSinceMs) / 60000);

    lastAttemptMs = now;
    poorSinceMs = 0;

    // Counted here, before anything can go wrong, so every attempt is visible even if it
    // returns early below.
    portENTER_CRITICAL(&s_link_mux);
    s_link.roamAttempts++;
    s_link.roamLastAttemptMs = now;
    portEXIT_CRITICAL(&s_link_mux);

    Log.notice("WiFi average has been %d dBm for %u min on %s; scanning for a better access point.\r\n",
               (int)avg, poorMinutes, (const char *)cur.ssid);

    /*
     * Blocking scan. It parks loopTask for a couple of seconds, which is safe: the sender
     * heartbeat may age by the scan plus the re-association below - about twelve seconds at
     * worst - against a senderStaleRebootSec floor of 60 s, so it cannot provoke a recovery
     * reboot. Anything slower than that would need the heartbeat refreshed from here.
     *
     * The component's event handler also sets its own scan-done bit when this completes.
     * Harmless, unless a web UI scan is running concurrently, in which case the two can
     * consume each other's results and both report nothing useful.
     */
    wifi_roam_set_phase(RoamPhase::SCANNING);

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        Log.warning("WiFi access point scan failed to start; leaving the association alone.\r\n");
        wifi_roam_note_result(RoamResult::SCAN_FAILED);
        return;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) {
        wifi_roam_note_result(RoamResult::NO_CANDIDATE);
        return;
    }

    if (found > WIFI_ROAM_MAX_SCAN_RECORDS)
        found = WIFI_ROAM_MAX_SCAN_RECORDS;

    // Heap, not stack: loopTask has 8 KB and these records are ~80 bytes each.
    wifi_ap_record_t *list =
        (wifi_ap_record_t *)calloc(found, sizeof(wifi_ap_record_t));
    if (list == nullptr) {
        esp_wifi_clear_ap_list();
        wifi_roam_note_result(RoamResult::OUT_OF_MEMORY);
        return;
    }

    esp_wifi_scan_get_ap_records(&found, list);

    int best = -1;
    for (uint16_t i = 0; i < found; i++) {
        // Same network only, and never the radio we are already on.
        if (strncmp((const char *)list[i].ssid, (const char *)cur.ssid,
                    sizeof(cur.ssid)) != 0)
            continue;
        if (memcmp(list[i].bssid, cur.bssid, 6) == 0)
            continue;
        if (best < 0 || list[i].rssi > list[best].rssi)
            best = i;
    }

    /*
     * Beat the better of the live sample and the window average.
     *
     * The candidate figure is a single scan sample, so comparing it against a ten minute
     * average alone would flatter it. Requiring it to clear both stops a lucky sample from
     * a marginally-different radio triggering a pointless reconnect.
     */
    const int8_t bar = (cur.rssi > avg) ? cur.rssi : avg;

    if (best < 0 || (list[best].rssi - bar) < WIFI_ROAM_MIN_GAIN_DBM) {
        Log.notice("No better access point for %s: best alternative %d dBm against %d dBm here. "
                   "The signal is simply weak everywhere; staying put.\r\n",
                   (const char *)cur.ssid,
                   best < 0 ? 0 : (int)list[best].rssi, (int)bar);
        free(list);
        wifi_roam_note_result(RoamResult::NO_CANDIDATE);
        return;
    }

    uint8_t target[6];
    memcpy(target, list[best].bssid, 6);
    const int8_t  targetRssi    = list[best].rssi;
    const uint8_t targetChannel = list[best].primary;
    free(list);

    Log.notice("Moving off %02x:%02x:%02x:%02x:%02x:%02x (%d dBm); best alternative is "
               "%02x:%02x:%02x:%02x:%02x:%02x (%d dBm, channel %u).\r\n",
               cur.bssid[0], cur.bssid[1], cur.bssid[2], cur.bssid[3], cur.bssid[4], cur.bssid[5],
               (int)cur.rssi,
               target[0], target[1], target[2], target[3], target[4], target[5],
               (int)targetRssi, (unsigned)targetChannel);

    // Tell the observer half this drop is ours, so it is not counted as a dropout.
    portENTER_CRITICAL(&s_link_mux);
    s_link.roamInProgress = true;
    s_link.roamPhase = RoamPhase::RECONNECTING;
    portEXIT_CRITICAL(&s_link_mux);

    /*
     * Drop the association and let the MANAGER reconnect. We deliberately do not connect
     * ourselves, and deliberately do not pin a BSSID.
     *
     * The earlier version did both: it pinned the target with bssid_set and called
     * esp_wifi_connect() directly. That worked - it moved a link from -86 dBm to -52 dBm on
     * hardware - but it left the manager desynchronised every single time, because the
     * manager's own reconnect wakes five seconds later, rewrites the station config as {0},
     * calls esp_wifi_connect() on an already-connected STA, fails three times and concludes
     * it is disconnected. Measured at 14,072 flag disagreements across two moves, with mDNS
     * unregistered and network_is_usable() carrying every upload on its netif fallback
     * meanwhile.
     *
     * Pinning is also no longer necessary. The component this firmware builds against is
     * patched to use WIFI_ALL_CHANNEL_SCAN with WIFI_CONNECT_AP_BY_SIGNAL, so the manager's
     * own connect sequence already picks the strongest access point on the SSID - which is
     * exactly what the pin was for. Letting it do the work keeps its state machine coherent,
     * which is worth more than choosing the radio ourselves.
     *
     * The scan above is still needed: without it we would tear down a working link whenever
     * the signal was poor, including when it is poor everywhere and there is nothing better
     * to move to.
     */
    esp_wifi_disconnect();

    /*
     * Wait for the manager to bring it back. It backs off WIFI_ROAM_MANAGER_BACKOFF_MS
     * before even trying, so this has to outlast that plus an association and DHCP.
     *
     * Worst case blocking on loopTask is the scan plus this wait, about 29 s, against the
     * 60 s floor on senderStaleRebootSec - so it still cannot provoke a recovery reboot.
     */
    bool landed = false;
    bool reassociated = false;
    uint8_t landedBssid[6] = {0};
    int8_t  landedRssi = 0;

    const uint64_t deadline = link_millis() + WIFI_ROAM_REASSOC_TIMEOUT_MS;
    while (link_millis() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(500));

        wifi_ap_record_t now_ap;
        if (esp_wifi_sta_get_ap_info(&now_ap) != ESP_OK)
            continue;                       // still down, keep waiting

        reassociated = true;
        memcpy(landedBssid, now_ap.bssid, 6);
        landedRssi = now_ap.rssi;

        // Landed somewhere other than where we started is the outcome we wanted. Coming back
        // to the same radio means the move achieved nothing.
        landed = (memcmp(now_ap.bssid, cur.bssid, 6) != 0);
        break;
    }

    portENTER_CRITICAL(&s_link_mux);
    s_link.roamInProgress = false;
    portEXIT_CRITICAL(&s_link_mux);

    if (landed) {
        wifi_roam_note_result(RoamResult::LANDED);
        Log.notice("Moved to %02x:%02x:%02x:%02x:%02x:%02x (%d dBm).\r\n",
                   landedBssid[0], landedBssid[1], landedBssid[2],
                   landedBssid[3], landedBssid[4], landedBssid[5], (int)landedRssi);
    } else if (reassociated) {
        /*
         * Back on the same radio. Either it really is the strongest despite the scan saying
         * otherwise - scan and association see the air a second apart - or the alternative
         * refused us. Either way the link is up and nothing is broken; wait out the cooldown.
         */
        wifi_roam_note_result(RoamResult::SAME_AP);
        Log.warning("Reconnected to the same access point (%d dBm); the move achieved "
                    "nothing. Next attempt in %u min.\r\n",
                    (int)landedRssi, (unsigned)(WIFI_ROAM_COOLDOWN_MS / 60000));
    } else {
        /*
         * Still down when the budget ran out. The manager's auto-reconnect is armed and
         * untouched - we stood the link down with esp_wifi_disconnect(), not
         * wifi_cfg_disconnect(), which suppresses reconnection until something asks to
         * connect again - so it keeps retrying on its own backoff and the queue covers the
         * gap.
         */
        wifi_roam_note_result(RoamResult::REASSOC_TIMEOUT);
        Log.warning("Still disconnected %u s after standing down; leaving it to the "
                    "manager's auto-reconnect.\r\n",
                    (unsigned)(WIFI_ROAM_REASSOC_TIMEOUT_MS / 1000));
    }
}
