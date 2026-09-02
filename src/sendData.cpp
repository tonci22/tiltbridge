#include <ctime>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <thorlog.h>
#include <ArduinoJson.h>

#include "url_utils.h"
#include "targets/send_json_str.h"

#include "tilt/tiltScanner.h"
#include "mqtt_client.h"  // for init_mqtt()
#include "jsonconfig.h"
#include "http_server.h"
#include "main.h"  // for printMem()
#include "wifi_setup.h"

#include "sendData.h"
#include "sender_health.h"
#include "device_config.h"
#include "queue/reading_queue.h"
#include "time_sync.h"
#include "uptime.h"


dataSendHandler data_sender; // Global data sender

const char* const sendTargetNames[TARGET_COUNT] = {
    "legacy_fermentrack",
    "fermentrack",
    "brewers_friend",
    "brewfather",
    "user_target",
    "grainfather",
    "brew_status",
    "taplistio",
    "google_sheets",
    "mqtt",
    "influxdb"
};

dataSendHandler::dataSendHandler() {}

void dataSendHandler::setTargetStatus(SendTargetID target, SendError error) {
    if (target < TARGET_COUNT) {
        targetStatus[target].lastError = error;
        /*
         * uptimeSeconds() returns the SECONDS COMPONENT of a d/h/m/s breakdown - 0..59 - not
         * total uptime, so this field never held what its name and /api/errors/ claim. Any
         * consumer differencing it against "now" got a value that wrapped every minute.
         */
        targetStatus[target].lastAttemptTime = sh_millis() / 1000;

        if (error == SEND_OK) {
            targetStatus[target].consecutiveFailures = 0;
        } else if (targetStatus[target].consecutiveFailures < UINT16_MAX) {
            targetStatus[target].consecutiveFailures++;
        }

        // Every sender routes its result through here, so this is the one place that needs
        // to feed the health monitor.
        sender_health.noteTargetResult((uint8_t)target, error == SEND_OK);
    }
}

uint32_t dataSendHandler::backoffDelay(SendTargetID target, uint32_t baseSeconds) const {
    if (target >= TARGET_COUNT)
        return baseSeconds;

    const uint16_t failures = targetStatus[target].consecutiveFailures;
    if (failures <= SEND_BACKOFF_AFTER_FAILURES)
        return baseSeconds;

    // Double per failure past the threshold, capped. Shifting beyond 16 would overflow, and
    // the cap is reached long before that anyway.
    const uint16_t steps = (failures - SEND_BACKOFF_AFTER_FAILURES) > 16
                         ? 16
                         : (uint16_t)(failures - SEND_BACKOFF_AFTER_FAILURES);

    uint64_t delay = (uint64_t)baseSeconds << steps;
    if (delay > SEND_BACKOFF_MAX_SECONDS)
        delay = SEND_BACKOFF_MAX_SECONDS;

    if (delay != baseSeconds) {
        Log.verbose("%s backing off to %us after %u consecutive failures.\r\n",
                    sendTargetNames[target], (unsigned)delay, (unsigned)failures);
    }
    return (uint32_t)delay;
}

SendError dataSendHandler::httpCodeToSendError(int16_t httpCode) {
    if (httpCode >= 200 && httpCode <= 204) return SEND_OK;
    if (httpCode == -1) return SEND_ERR_CONNECTION_FAILED;
    switch (httpCode) {
        case 400: return SEND_ERR_BAD_REQUEST;
        case 401:
        case 403: return SEND_ERR_AUTH_FAILED;
        case 404: return SEND_ERR_NOT_FOUND;
        case 429: return SEND_ERR_RATE_LIMITED;
        default:
            if (httpCode >= 500) return SEND_ERR_SERVER_ERROR;
            return SEND_ERR_OTHER;
    }
}

// Timer callback functions for FreeRTOS software timers
// These are static/free functions that set the semaphore flags
static void legacyFermentrackTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_legacy_fermentrack = true;
}

static void fermentrackTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_fermentrack = true;
}

static void mqttTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_mqtt = true;
}

static void brewStatusTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_brewStatus = true;
}

static void brewfatherTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_brewfather = true;
}

static void brewersFriendTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_brewersFriend = true;
}

static void userTargetTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_userTarget = true;
}

static void gSheetsTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_gSheets = true;
}

static void grainfatherTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_grainfather = true;
}

static void taplistioTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_taplistio = true;
}

static void influxdbTimerCallback(TimerHandle_t xTimer) {
    data_sender.send_influxdb = true;
}

static void queueSnapshotTimerCallback(TimerHandle_t xTimer) {
    data_sender.snapshot_due = true;
}

void dataSendHandler::createTimers() {
    // Create all send timers as one-shot timers (pdFALSE)
    // Initial period is 1 tick - we'll change it when we start the timer
    legacyFermentrackTimer = xTimerCreate("LegacyFT", pdMS_TO_TICKS(1000), pdFALSE, nullptr, legacyFermentrackTimerCallback);
    fermentrackTimer = xTimerCreate("Fermentrack", pdMS_TO_TICKS(1000), pdFALSE, nullptr, fermentrackTimerCallback);
    mqttTimer = xTimerCreate("MQTT", pdMS_TO_TICKS(1000), pdFALSE, nullptr, mqttTimerCallback);
    brewStatusTimer = xTimerCreate("BrewStatus", pdMS_TO_TICKS(1000), pdFALSE, nullptr, brewStatusTimerCallback);
    brewfatherTimer = xTimerCreate("Brewfather", pdMS_TO_TICKS(1000), pdFALSE, nullptr, brewfatherTimerCallback);
    brewersFriendTimer = xTimerCreate("BrewersFriend", pdMS_TO_TICKS(1000), pdFALSE, nullptr, brewersFriendTimerCallback);
    userTargetTimer = xTimerCreate("UserTarget", pdMS_TO_TICKS(1000), pdFALSE, nullptr, userTargetTimerCallback);
    gSheetsTimer = xTimerCreate("GSheets", pdMS_TO_TICKS(1000), pdFALSE, nullptr, gSheetsTimerCallback);
    grainfatherTimer = xTimerCreate("Grainfather", pdMS_TO_TICKS(1000), pdFALSE, nullptr, grainfatherTimerCallback);
    taplistioTimer = xTimerCreate("Taplistio", pdMS_TO_TICKS(1000), pdFALSE, nullptr, taplistioTimerCallback);
    influxdbTimer = xTimerCreate("InfluxDB", pdMS_TO_TICKS(1000), pdFALSE, nullptr, influxdbTimerCallback);
    queueSnapshotTimer = xTimerCreate("QueueSnap", pdMS_TO_TICKS(1000), pdFALSE, nullptr, queueSnapshotTimerCallback);
}

void dataSendHandler::startTimer(TimerHandle_t timer, uint32_t periodSeconds) {
    if (timer == nullptr)
        return;

    // Ticks are computed here rather than with pdMS_TO_TICKS(periodSeconds * 1000).
    // On the non-SMP kernel that macro multiplies by configTICK_RATE_HZ in 32-bit
    // arithmetic, so a millisecond value above ~4,294,967 (about 4294 s) overflows and
    // yields a wildly short period - a 6-hour interval became ~125 s. Several settings
    // permit periods past that: queueSnapshotIntervalSec allows 21600 and
    // legacyFermentrackPushEvery allows 43200.
    uint64_t ticks = (uint64_t)periodSeconds * (uint64_t)configTICK_RATE_HZ;
    if (ticks == 0)
        ticks = 1;                          // a zero period is rejected by the timer API
    if (ticks > (uint64_t)(portMAX_DELAY - 1))
        ticks = (uint64_t)(portMAX_DELAY - 1);

    // Stop the timer first (without triggering callback) to ensure clean restart
    xTimerStop(timer, 0);
    // Change period and start - xTimerChangePeriod implicitly starts the timer
    xTimerChangePeriod(timer, (TickType_t)ticks, 0);
}

/**
 * @brief Re-arm the Google Sheets timer against a fixed grid. See the declaration.
 *
 * startTimer() counts from the moment it is called, so re-arming after an upload makes the
 * real period `interval + upload duration`. Here the deadline is absolute: it advances by
 * whole intervals, and the timer is given only the remainder. A push that took 20 s
 * therefore leaves 9m40s, not 10m, and the cadence does not walk.
 *
 * Only the configured cadence is gridded. A drain burst or a backoff delay is deliberately
 * off-cadence, so it drops the anchor and the next normal push lays out a fresh grid from
 * wherever it lands - otherwise the first push after an outage could snap onto a stale grid
 * point and fire immediately.
 */
void dataSendHandler::rearmGSheetsTimer(uint32_t periodSeconds)
{
    if (periodSeconds == 0)
        periodSeconds = 1;

    const uint32_t now = sh_millis();
    const uint32_t intervalMs = periodSeconds * 1000UL;

    /*
     * Only the configured cadence gets a grid. A drain burst or a backoff delay is
     * deliberately off-cadence: honour it exactly and drop the anchor, so that the next
     * normal push lays out a fresh grid from wherever it lands instead of snapping onto a
     * stale grid point and firing immediately.
     */
    if (periodSeconds != config.gsheetsPushEvery) {
        gSheetsNextDueMs = 0;
        gSheetsGridIntervalSec = 0;
        startTimer(gSheetsTimer, periodSeconds);
        return;
    }

    // No grid yet, or the push interval was changed in the UI: start one from here.
    if (gSheetsNextDueMs == 0 || gSheetsGridIntervalSec != periodSeconds) {
        gSheetsGridIntervalSec = periodSeconds;
        gSheetsNextDueMs = now + intervalMs;
        startTimer(gSheetsTimer, periodSeconds);
        return;
    }

    /*
     * Advance the deadline to the next grid point still ahead of us.
     *
     * The subtraction is unsigned, which stays correct across the ~49.7-day wrap of the
     * millisecond counter: a deadline that has already passed shows up as a very large
     * difference, which is exactly the "> intervalMs" case the loop consumes. Bounded so
     * that a nonsense value can never spin here.
     */
    uint32_t remaining = gSheetsNextDueMs - now;

    for (uint16_t guard = 0; remaining > intervalMs && guard < 1000; guard++) {
        gSheetsNextDueMs += intervalMs;
        remaining = gSheetsNextDueMs - now;
    }

    if (remaining > intervalMs) {
        // Gave up stepping. Re-anchor rather than schedule something meaningless.
        gSheetsNextDueMs = now + intervalMs;
        remaining = intervalMs;
    }

    /*
     * The push finished right on top of the next grid point, which happens when it returned
     * early without doing any network work. Firing now would push twice in a row for no
     * benefit, so take the following point: still on the grid, one interval later.
     */
    if (remaining < 2000) {
        gSheetsNextDueMs += intervalMs;
        remaining += intervalMs;
    }

    /*
     * Rounded up, because the timer takes whole seconds. Rounding down would fire a
     * fraction of a second EARLY, leaving the deadline still in the future on the next
     * pass - and this function would then schedule that fraction as the next delay.
     */
    startTimer(gSheetsTimer, (remaining + 999) / 1000);
}

void dataSendHandler::init()
{
    init_mqtt();

    // Create all FreeRTOS timers
    createTimers();

    // Schedule first sends with staggered delays to avoid overwhelming the system
    startTimer(legacyFermentrackTimer, 12);      // Schedule first send to Legacy Fermentrack
    startTimer(fermentrackTimer, 10);            // Schedule first send to Fermentrack
    startTimer(mqttTimer, 20);                   // Schedule first send to MQTT
    startTimer(brewStatusTimer, 30);             // Schedule first send to Brew Status
    startTimer(brewfatherTimer, 40);             // Schedule first send to Brewfather
    startTimer(brewersFriendTimer, 50);          // Schedule first send to Brewer's Friend
    startTimer(userTargetTimer, 60);             // Schedule first send to User-defined JSON target
    startTimer(gSheetsTimer, 70);                // Schedule first send to Google Sheets
    startTimer(grainfatherTimer, 80);            // Schedule first send to Grainfather
    startTimer(taplistioTimer, 90);              // Schedule first send to Taplist.io
    startTimer(influxdbTimer, 100);              // Schedule first send to InfluxDB
    startTimer(queueSnapshotTimer, 120);         // First queue snapshot shortly after boot,
                                                 // then config.queueSnapshotIntervalSec
}

void dataSendHandler::process()
{
#ifdef TB_DEBUG_FREEZE
    // Acceptance-test hook only: simulate a wedged outbound loop. Must return BEFORE the
    // heartbeat, since a stale heartbeat is exactly what is being simulated.
    // See docs/phase1/10-acceptance-tests.md T6.
    if (sender_health.debugFreeze)
        return;
#endif

    // Unconditional, and before any gate below: this is the only signal that proves the
    // outbound loop is still being serviced. The health monitor reboots the device if it
    // stops advancing while BLE and WiFi are alive.
    sender_health.heartbeat();

    // Snapshots are taken before the network gate and hold no sender lock: the whole
    // point of the queue is to keep accumulating while the network is down, and a wedged
    // HTTP request must not be able to stop it.
    if (snapshot_due) {
        snapshot_due = false;
        take_queue_snapshot();
    }

    if (network_is_usable()) {
        // Google Sheets runs first so that within a single pass it claims the sender
        // before either Fermentrack target can, per the requirement that a stuck
        // Fermentrack request must not keep other targets from operating.
        if (config.gsheetsV2Enabled)
            send_to_google_v2();
        else
            send_to_google();

        // The heartbeat is refreshed between targets, not just at the top of the pass.
        // Completing a target IS progress, and after an outage every timer can be due at
        // once - a legitimate pass can then block for longer than senderStaleRebootSec and
        // would otherwise trip a spurious recovery reboot. A target wedged inside its own
        // HTTP call still never reaches the next heartbeat, which is the case we detect.
        sender_health.heartbeat();
        send_to_legacy_fermentrack();
        sender_health.heartbeat();
        send_to_fermentrack();
        sender_health.heartbeat();
        send_to_bf_and_bf();
        sender_health.heartbeat();
        send_to_grainfather();
        sender_health.heartbeat();
        send_to_brewstatus();
        sender_health.heartbeat();
        send_to_taplistio();
        sender_health.heartbeat();
        send_to_mqtt();
        sender_health.heartbeat();
        send_to_influxdb();
    }
}


/**
 * @brief Build a QueuedReading for every enabled Tilt that has a usable gravity.
 *
 * Reads whatever the scanner holds right now, which is what makes it usable for both
 * paths: the live sender wants the newest values, and the persistence path wants the
 * newest values at the moment persistence falls due. Neither writes to flash here, and
 * neither resets the RSSI interval - that only happens once a record is durably stored or
 * actually delivered, so a failure keeps accumulating instead of losing the window.
 *
 * @return number of records written to `out`, never more than maxRecords.
 */
uint16_t dataSendHandler::collectCurrentReadings(QueuedReading *out, uint16_t maxRecords)
{
    if (out == nullptr || maxRecords == 0)
        return 0;

    tilt_scanner.drop_expired_tilts();

    uint16_t collected = 0;
    uint16_t unconfigured = 0;

    for (tiltHydrometer &th : tilt_scanner.m_tilt_devices) {
        if (collected >= maxRecords)
            break;

        if (!device_config.isEnabled(th.deviceId()))
            continue;

        /*
         * Never send a Tilt that has not been configured yet.
         *
         * Without this, a Tilt seen before its configuration exists is filed under a derived
         * placeholder ("Red-8C1C"), and because the sheet name is snapshotted into the record
         * at capture time, configuring it later does not correct the readings already taken -
         * they still arrive and create a junk sheet. Setting up a device then became a race
         * against the upload interval.
         *
         * Skipping instead means setup order does not matter: nothing is written for a Tilt
         * until it has a home, and it starts flowing the moment one is saved. A device that
         * HAS a config but no sheet name is untouched here - that is a deliberate choice made
         * in front of the field, and it falls back to the colour as before.
         */
        if (device_config.find(th.deviceId()) == nullptr) {
            unconfigured++;
            continue;
        }

        // Never queue a Tilt that has not actually reported a gravity yet - that would
        // persist a zero and pollute the sheet.
        if (th.latest_gravity_value() == 0)
            continue;

        QueuedReading &rec = out[collected];
        memset(&rec, 0, sizeof(rec));   // deterministic padding keeps the CRC stable

        reading_queue.assignIdentity(rec, th.deviceId());

        strlcpy(rec.deviceId, th.deviceId(), sizeof(rec.deviceId));
        rec.colorIndex = th.m_color;
        device_config.sheetName(th.deviceId(), th.m_color, rec.sheetName, sizeof(rec.sheetName));

        char buf[12];
        th.converted_temp(buf, sizeof(buf), true);      // always Fahrenheit
        rec.tempF = strtof(buf, nullptr);

        // `gravity` is the normal final TiltBridge value; the other two are the existing
        // intermediates. No smoothing or calibration behaviour changes here.
        th.cal_smooth_gravity_str(buf, sizeof(buf));
        rec.gravity = strtof(buf, nullptr);
        th.uncal_smooth_gravity_str(buf, sizeof(buf));
        rec.gravitySmoothed = strtof(buf, nullptr);
        th.latest_gravity_str(buf, sizeof(buf));
        rec.gravityRaw = strtof(buf, nullptr);

        rec.rssiLatest  = th.rssi;
        rec.rssiAverage = th.rssi_stats.average();
        rec.rssiMinimum = th.rssi_stats.minimum;
        rec.rssiMaximum = th.rssi_stats.maximum;
        rec.rssiSamples = th.rssi_stats.samples;

        if (th.tilt_pro)
            rec.flags |= QR_FLAG_TILT_PRO;

        // Never fabricate a timestamp. Ordering is preserved by the sequence number and
        // by the uptime-relative capture time regardless.
        if (time_is_valid()) {
            rec.capturedAtUtc = utc_now();
            rec.flags |= QR_FLAG_TIMESTAMP_VALID;
        }
        rec.capturedAtUptimeMs = sh_millis();

        collected++;
    }

    /*
     * Say so, but not every cycle. Silence would be worse than the junk sheets: a Tilt that
     * is being seen and deliberately not sent must be visible somewhere. The Tilts page also
     * shows hasDeviceConfig=false for these.
     */
    if (unconfigured > 0) {
        const uint32_t now = sh_millis();
        if (m_lastUnconfiguredWarnMs == 0 || (now - m_lastUnconfiguredWarnMs) > 300000) {
            m_lastUnconfiguredWarnMs = now;
            Log.warning("%u Tilt%s seen but not configured; nothing is sent for them until you "
                        "set them up on the Tilts page.\r\n",
                        (unsigned)unconfigured, (unconfigured == 1) ? " is" : "s are");
        }
    }

    return collected;
}

/**
 * @brief Start a fresh RSSI aggregation window for the Tilts in `batch`.
 *
 * Called only once those records are accounted for - durably on flash, or accepted by the
 * server. Until then the window keeps accumulating, so a failure does not silently narrow
 * the min/max/sample count of the reading that eventually gets through.
 */
void dataSendHandler::resetCollectedRssiIntervals(const QueuedReading *batch, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        for (tiltHydrometer &th : tilt_scanner.m_tilt_devices) {
            if (strcmp(th.deviceId(), batch[i].deviceId) == 0) {
                th.rssi_stats.resetInterval();
                break;
            }
        }
    }
}

/**
 * @brief Persist the current readings to flash.
 *
 * ONLY runs when the live sender is not delivering. In steady state every reading is sent
 * and dropped without ever touching flash; this is the fallback that makes an outage
 * survivable, and its cadence (config.queueSnapshotIntervalSec) is what decides how much
 * of an outage fits in maxQueuedRecords. Coarser here means fewer rows kept but a longer
 * runway before the queue overflows - the whole point of separating it from the push
 * interval.
 */
void dataSendHandler::take_queue_snapshot()
{
    // Always re-arm, even when disabled or when nothing gets written, so the cadence
    // survives a configuration change or a temporary filesystem problem.
    startTimer(queueSnapshotTimer, config.queueSnapshotIntervalSec);

    if (!config.offlineQueueEnabled || !reading_queue.isHealthy())
        return;

    // Nothing to fall back to while the live path is delivering and no backlog is waiting.
    // A backlog counts because the live path stands down until it drains, so these are the
    // only readings being captured in that window.
    if (!queuePersistenceNeeded())
        return;

    uint16_t maxRecords = config.queueBatchSize;
    if (maxRecords == 0) maxRecords = 1;
    if (maxRecords < TILT_COLORS) maxRecords = TILT_COLORS;

    QueuedReading *batch = (QueuedReading *)malloc(maxRecords * sizeof(QueuedReading));
    if (batch == nullptr) {
        Log.error("Queue snapshot: unable to allocate a %u-record buffer.\r\n", (unsigned)maxRecords);
        return;
    }

    const uint16_t collected = collectCurrentReadings(batch, maxRecords);

    uint16_t stored = 0;
    for (uint16_t i = 0; i < collected; i++) {
        if (reading_queue.append(batch[i])) {
            // Only start a new RSSI interval once the record is durably on flash; if the
            // write failed, keep accumulating so the next snapshot still covers the gap.
            resetCollectedRssiIntervals(&batch[i], 1);
            stored++;
        }
    }

    free(batch);

    if (stored > 0)
        Log.notice("Queue snapshot: stored %u reading%s.\r\n", stored, (stored == 1) ? "" : "s");

    /*
     * A snapshot that collects readings and stores none of them used to be completely
     * silent: the only log line was guarded by `stored > 0`. That is the shape of a
     * disappearance - readings are collected, given record ids, and then dropped with
     * nothing said, on the device or anywhere else - so it is now an error in its own right.
     */
    if (stored < collected)
        Log.error("Queue snapshot: queue REFUSED %u of %u readings - those are lost. "
                  "Queue healthy %d, pending %u.\r\n",
                  (unsigned)(collected - stored), (unsigned)collected,
                  (int)reading_queue.isHealthy(), (unsigned)reading_queue.pendingCount());
}

/**
 * @brief Whether readings currently need to be written to flash.
 *
 * True when the Google Sheets sender is failing, or when a backlog is already waiting -
 * the queue is that target's alone (nothing else reads it), and no other target can accept
 * a backlog anyway, because only the schemaVersion 2 payload carries a capture time.
 */
bool dataSendHandler::queuePersistenceNeeded() const
{
    if (reading_queue.pendingCount() > 0)
        return true;

    // Legacy single-reading mode never drains the queue, so filling it would be a leak.
    if (!config.gsheetsV2Enabled)
        return false;

    // No usable network means the sender is never even called, so there is no failure count
    // to read - but every reading is undeliverable. Miss this and a WiFi outage, the single
    // most likely reason to need the queue, would silently lose everything.
    if (!network_is_usable())
        return true;

    return targetStatus[TARGET_GOOGLE_SHEETS].consecutiveFailures > 0;
}

bool dataSendHandler::send_to_bf_and_bf()
{
    bool retval = false;
    if (data_sender.send_brewersFriend)
    {
        SenderLock lock(TARGET_BREWERS_FRIEND, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return retval;  // Another target holds the sender; retry on the next pass

        // Brewer's Friend
        data_sender.send_brewersFriend = false;
        if (strlen(config.brewersFriendKey) > BREWERS_FRIEND_MIN_KEY_LENGTH) {
            Log.verbose("Calling send to Brewer's Friend.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_BREWERS_FRIEND);
            if (retval)
            {
                Log.notice("Completed send to Brewer's Friend.\r\n");
            }
            else
            {
                Log.verbose("Error sending to Brewer's Friend.\r\n");
            }
        }
        startTimer(brewersFriendTimer, backoffDelay(TARGET_BREWERS_FRIEND, config.brewersFriendPushEvery)); // Set up subsequent send to Brewer's Friend
    }

    if (data_sender.send_brewfather)
    {
        SenderLock lock(TARGET_BREWFATHER, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return retval;

        // Brewfather
        data_sender.send_brewfather = false;
        if (strlen(config.brewfatherKey) > BREWFATHER_MIN_KEY_LENGTH) {
            Log.verbose("Calling send to Brewfather.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_BREWFATHER);
            if (retval)
            {
                Log.notice("Completed send to Brewfather.\r\n");
            }
            else
            {
                Log.verbose("Error sending to Brewfather.\r\n");
            }
        }
        startTimer(brewfatherTimer, backoffDelay(TARGET_BREWFATHER, config.brewfatherPushEvery)); // Set up subsequent send to Brewfather
    }


    if (data_sender.send_userTarget)
    {
        SenderLock lock(TARGET_USER_TARGET, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return retval;

        // User Target
        data_sender.send_userTarget = false;
        if (strlen(config.userTargetURL) > USER_TARGET_MIN_URL_LENGTH)
        {
            Log.verbose("Calling send to User Target.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_USER_TARGET);
            if (retval)
            {
                Log.notice("Completed send to User Target.\r\n");
            }
            else
            {
                Log.verbose("Error sending to User Target.\r\n");
            }
        }
        startTimer(userTargetTimer, backoffDelay(TARGET_USER_TARGET, config.userTargetPushEvery)); // Set up subsequent send to User Target
    }
    return retval;
}

bool dataSendHandler::send_to_bf_and_bf(const uint8_t which_bf)
{
    // This function combines the data formatting for both "BF"s - Brewers
    // Friend & Brewfather. Once the data is formatted, it is dispatched
    // to send_to_url to be sent out.

    bool result = true;
    JsonDocument j;
    char url[128];
    int16_t httpCode = 0;

    SendTargetID targetId = (which_bf == BF_MEANS_BREWFATHER) ? TARGET_BREWFATHER :
                            (which_bf == BF_MEANS_BREWERS_FRIEND) ? TARGET_BREWERS_FRIEND :
                            TARGET_USER_TARGET;

    // As this function is being used for both Brewer's Friend and Brewfather,
    // let's determine which we want and set up the URL/API key accordingly.
    if (which_bf == BF_MEANS_BREWFATHER)
    {
        if (strlen(config.brewfatherKey) <= BREWFATHER_MIN_KEY_LENGTH)
        {
            Log.verbose("Brewfather key not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, "http://log.brewfather.net/stream?id=");
        strcat(url, config.brewfatherKey);
    }
    else if (which_bf == BF_MEANS_BREWERS_FRIEND)
    {
        if (strlen(config.brewersFriendKey) <= BREWERS_FRIEND_MIN_KEY_LENGTH)
        {
            Log.verbose("Brewer's Friend key not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, "https://log.brewersfriend.com/stream/");
        strcat(url, config.brewersFriendKey);
    }
    else if (which_bf == BF_MEANS_USER_TARGET)
    {
        if (strlen(config.userTargetURL) <= USER_TARGET_MIN_URL_LENGTH)
        {
            Log.verbose("User target URL not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, config.userTargetURL);
    }
    else
    {
        Log.error("Invalid value of which_bf passed to send_to_bf_and_bf.\r\n");
        return false;
    }

    // Loop through each of the tilt colors cached by tilt_scanner, sending
    // data for each of the active tilts
    tilt_scanner.drop_expired_tilts();
    bool attempted = false;
    for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
        if (!device_config.isEnabled(th.deviceId()))
            continue;

        char gravity[10];
        char temp[6];

        Log.verbose("Tilt loaded with color name: %s\r\n", tilt_color_names[th.m_color]);
        // Falls back to the colour name when no friendly name is configured.
        j["name"] = device_config.displayName(th.deviceId(), th.m_color);
        th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
        j["temp"] = temp;
        j["temp_unit"] = "F";
        th.cal_smooth_gravity_str(gravity, sizeof(gravity));
        j["gravity"] = gravity;
        j["gravity_unit"] = "G";
        j["device_source"] = "TiltBridge";

        char payload_string[BF_SIZE];
        serializeJson(j, payload_string);

        attempted = true;
        if (http_request(url, httpMethod::HTTP_POST, payload_string, &httpCode) != sendResult::success)
            result = false; // There was an error with the previous send
    }
    // If we tried to send, always update status so a recovered connection clears
    // any stale error. httpCode can remain 0 when http_request bails early
    // (WiFi down, mDNS resolution failure, client init failure) — treat that
    // as a connection failure rather than leaving the previous status cached.
    if (attempted)
        setTargetStatus(targetId, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
    return result;
}

bool dataSendHandler::send_to_grainfather()
{
    bool result = true;

    if (send_grainfather)
    {
        SenderLock lock(TARGET_GRAINFATHER, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return result;

        // Brew Status
        send_grainfather = false;
        int16_t httpCode = 0;

        // Loop through each of the tilt colors cached by tilt_scanner, sending
        // data for each of the active tilts
        tilt_scanner.drop_expired_tilts();
        bool attempted = false;
        for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
            if (!device_config.isEnabled(th.deviceId()))
                continue;

            // If there's no Grainfather URL for this color, just continue
            if (strlen(config.grainfatherURL[th.m_color].link) == 0)
                continue;

            Log.verbose("Calling send to Grainfather.\r\n");
            char gravity[10];
            char temp[6];
            JsonDocument j;
            Log.verbose("Tilt loaded with color name: %s\r\n", tilt_color_names[th.m_color]);
            th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
            j["Temp"] = temp;
            j["Unit"] = "F";
            th.cal_smooth_gravity_str(gravity, sizeof(gravity));
            j["SG"] = gravity;

            char payload_string[GF_SIZE];
            serializeJson(j, payload_string);

            attempted = true;
            if (http_request(config.grainfatherURL[th.m_color].link, httpMethod::HTTP_POST, payload_string, &httpCode) != sendResult::success)
                result = false; // There was an error with the previous send
        }
        if (attempted)
            setTargetStatus(TARGET_GRAINFATHER, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
        startTimer(grainfatherTimer, backoffDelay(TARGET_GRAINFATHER, config.grainfatherPushEvery)); // Set up subsequent send to Grainfather
    }
    return result;
}

bool dataSendHandler::send_to_taplistio()
{
    bool result = true;

    // Check if config.taplistioURL is set, and return if it's not
    if (strlen(config.taplistioURL) <= 10) {
        return false;
    }

    // See if it's our time to send.
    if (!send_taplistio) {
        return false;
    }

    SenderLock lock(TARGET_TAPLISTIO, HTTP_TIMEOUT_DEFAULT_MS);
    if (!lock) {
        Log.verbose("taplist.io: sender busy.\r\n");
        return false;
    }

    // Since we're using one-shot timers, stop the timer before restarting with new period
    if (taplistioTimer != nullptr) {
        xTimerStop(taplistioTimer, 0);
    }

    // Attempt to send.
    send_taplistio = false;


    tilt_scanner.drop_expired_tilts();
    int16_t httpCode = 0;
    bool attempted = false;

    for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
        if (!device_config.isEnabled(th.deviceId()))
            continue;

        JsonDocument j;
        char payload_string[192];
        char gravity[10];
        char temp[6];

        j["Color"] = tilt_color_names[th.m_color];
        th.converted_temp(temp, sizeof(temp), true);  // Always in Fahrenheit
        j["Temp"] = temp;
        th.cal_smooth_gravity_str(gravity, sizeof(gravity));
        j["SG"] = gravity;
        j["temperature_unit"] = "F";
        j["gravity_unit"] = "G";

        serializeJson(j, payload_string);

        Log.verbose("taplist.io: Sending %s Tilt to %s\r\n", tilt_color_names[th.m_color], config.taplistioURL);

        attempted = true;
        result = (http_request(config.taplistioURL, httpMethod::HTTP_POST, payload_string, &httpCode) == sendResult::success);
    }

    if (attempted)
        setTargetStatus(TARGET_TAPLISTIO, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
    startTimer(taplistioTimer, backoffDelay(TARGET_TAPLISTIO, config.taplistioPushEvery));
    return result;
}


bool dataSendHandler::send_to_brewstatus()
{
    bool result = true;
    const int payload_size = 512;
    char payload[payload_size];

    if (send_brewStatus)
    {
        SenderLock lock(TARGET_BREW_STATUS, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return result;

        // Brew Status
        send_brewStatus = false;
        if (strlen(config.brewstatusURL) > BREWSTATUS_MIN_URL_LENGTH) {
            Log.verbose("Calling send to Brew Status.\r\n");

            // The payload should look like this when sent to Brewstatus:
            // ('Request payload:', 'SG=1.019&Temp=71.0&Color=ORANGE&Timepoint=43984.33630927084&Beer=Beer&Comment=Comment')
            // BrewStatus ignores Beer, so we just set this to Undefined.
            // BrewStatus will record Comment if it set, but just leave it blank.
            // The Timepoint is Google Sheets time, which is fractional days since 12/30/1899
            // Using https://www.timeanddate.com/date/durationresult.html?m1=12&d1=30&y1=1899&m2=1&d2=1&y2=1970 gives
            // us 25,569 days from the start of Google Sheets time to the start of the Unix epoch.
            // BrewStatus wants local time, so we allow the user to specify a time offset.

            // Loop through each of the tilt colors cached by tilt_scanner, sending data for each of the active tilts
            tilt_scanner.drop_expired_tilts();
            int16_t httpCode = 0;
            bool attempted = false;
            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                if (!device_config.isEnabled(th.deviceId()))
                    continue;

                char gravity[10];
                char temp[6];
                th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit since we don't send units
                snprintf(payload, payload_size, "SG=%s&Temp=%s&Color=%s&Timepoint=%.11f&Beer=Undefined&Comment=",
                        gravity, temp, tilt_color_names[th.m_color], ((double)std::time(0) + (config.TZoffset * 3600.0)) / 86400.0 + 25569.0);

                HttpRequestOptions options;
                options.contentType = content_x_www_form_urlencoded;
                attempted = true;
                if (http_request(config.brewstatusURL, httpMethod::HTTP_POST, payload, nullptr, 0, options, &httpCode) == sendResult::success) {
                    Log.notice("Completed send to Brew Status.\r\n");
                } else {
                    result = false;
                    Log.verbose("Error sending to Brew Status.\r\n");
                }
            }
            if (attempted)
                setTargetStatus(TARGET_BREW_STATUS, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
        }
        startTimer(brewStatusTimer, backoffDelay(TARGET_BREW_STATUS, config.brewstatusPushEvery)); // Set up subsequent send to Brew Status
    }
    return result;
}


bool dataSendHandler::send_to_google()
{
    bool result = true;

    if (send_gSheets) {
        SenderLock lock(TARGET_GOOGLE_SHEETS, HTTP_TIMEOUT_GSHEETS_MS);
        if (!lock)
            return result;

        // Google Sheets
        send_gSheets = false;

        JsonDocument payload;
        char payload_string[GSHEETS_JSON];
        JsonDocument retval;
        int numSent = 0;

        // The google sheets handler only fires if we have both a Google Scripts URL to post to, and an email address.
        if (strlen(config.scriptsURL) >= GSCRIPTS_MIN_URL_LENGTH && strlen(config.scriptsEmail) >= GSCRIPTS_MIN_EMAIL_LENGTH) {
            Log.verbose("Checking for any pending Google Sheets pushes.\r\n");
            printMem();

            tilt_scanner.drop_expired_tilts();
            int16_t httpCode = 0;
            bool attempted = false;

            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                if (!device_config.isEnabled(th.deviceId()))
                    continue;

                // Check if there is a google sheet name associated with the specific Tilt.
                // Resolves to the device-specific name when configured, else the colour's.
                char sheetName[QR_SHEET_NAME_LEN];
                device_config.sheetName(th.deviceId(), th.m_color, sheetName, sizeof(sheetName));
                if (strlen(sheetName) > 0) {
                    char gravity[10];
                    char temp[6];

                    // If there's a sheet name saved, then we should send the data
                    if (numSent == 0)
                        Log.notice("Beginning GSheets check-in.\r\n");
                    payload["Beer"] = sheetName;
                    th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
                    payload["Temp"] = temp;
                    th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                    payload["SG"] = gravity;
                    payload["Color"] = tilt_color_names[th.m_color];
                    payload["Comment"] = "";
                    payload["Email"] = config.scriptsEmail; // The gmail email address associated with the script on google
                    payload["tzOffset"] = config.TZoffset;

                    serializeJson(payload, payload_string);
                    payload.clear();

                    Log.verbose("Sending the following payload to Google Sheets (%s):\r\n\t\t%s\r\n",
                               tilt_color_names[th.m_color], payload_string);

                    // Use unified http_request with response buffer to get doclongurl
                    char response[1024];
                    HttpRequestOptions options;
                    options.contentType = content_json;
                    options.skipCertValidation = true;
                    options.timeoutMs = 10000;  // 10 second timeout - Google Scripts can be slow

                    attempted = true;
                    sendResult sendRes = http_request(config.scriptsURL, httpMethod::HTTP_POST,
                                                      payload_string, response, sizeof(response), options, &httpCode);

                    if (sendRes == sendResult::success) {
                        // POST success - parse response for doclongurl
                        Log.verbose("HTTP Response: 200\r\nFull Response:\r\n\t%s\r\n", response);

                        // A 200 does not guarantee JSON - Apps Script happily returns an HTML
                        // error page. Check the parse and the key before dereferencing, or
                        // strcmp() faults on a null pointer and panics the device.
                        DeserializationError parseErr = deserializeJson(retval, response);
                        if (parseErr) {
                            Log.warning("Google response was not valid JSON (%s); skipping doclongurl update.\r\n",
                                        parseErr.c_str());
                        } else {
                            const char *docurl = retval["doclongurl"].as<const char *>();
                            if (docurl != nullptr) {
                                // Caches against the device entry when one exists, else
                                // against the colour, and only writes when it changed.
                                device_config.setSheetLink(th.deviceId(), th.m_color, docurl);
                            }
                        }
                        retval.clear();
                        numSent++;
                    } else {
                        // Post generated an error
                        Log.error("Google send to %s Tilt failed. Response:\r\n%s\r\n",
                            tilt_color_names[th.m_color], response);
                        result = false;
                    }

                    vTaskDelay(pdMS_TO_TICKS(100));  // Give some time between requests
                } // Check we have a sheet name for the color
            }

            Log.notice("Submitted %l sheet%s to Google.\r\n", numSent, (numSent== 1) ? "" : "s");
            if (attempted)
                setTargetStatus(TARGET_GOOGLE_SHEETS, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
        }
        // Anchored, like the v2 path: same timer, same target, so the two must not disagree
        // about what "every gsheetsPushEvery seconds" means.
        rearmGSheetsTimer(backoffDelay(TARGET_GOOGLE_SHEETS, config.gsheetsPushEvery));
    }
    return result;
}


bool dataSendHandler::send_to_influxdb()
{
    bool result = true;

    if (send_influxdb)
    {
        SenderLock lock(TARGET_INFLUXDB, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return result;

        send_influxdb = false;

        if (strlen(config.influxdbURL) > INFLUXDB_MIN_URL_LENGTH && strlen(config.influxdbToken) > 0 && strlen(config.influxdbOrg) > 0 && strlen(config.influxdbBucket) > 0) 
        {

            Log.verbose("Calling send to InfluxDB.\r\n");

            // Build the write API URL
            char writeURL[512];
            snprintf(writeURL, sizeof(writeURL), "%s/api/v2/write?org=%s&bucket=%s&precision=s",
                     config.influxdbURL, config.influxdbOrg, config.influxdbBucket);

            // Build line protocol data
            char lineData[2048];
            size_t lineDataLen = 0;
            lineData[0] = '\0';

            tilt_scanner.drop_expired_tilts();
            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                if (!device_config.isEnabled(th.deviceId()))
                    continue;

                char gravity[10];
                char temp[6];
                char battery_str[4];

                th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                th.converted_temp(temp, sizeof(temp), false); // Use configured unit
                th.get_weeks_battery(battery_str, sizeof(battery_str));

                // InfluxDB line protocol: measurement,tag1=value1 field1=value1,field2=value2 timestamp
                char line[256];
                int lineLen = snprintf(line, sizeof(line),
                        "tilt,color=%s,device_source=TiltBridge "
                        "gravity=%s,temperature=%s,temp_units=\"%s\",weeks_on_battery=%s\n",
                        tilt_color_names[th.m_color],
                        gravity, temp, config.tempUnit, battery_str);

                // Append to lineData if there's room
                if (lineDataLen + lineLen < sizeof(lineData) - 1) {
                    strlcat(lineData, line, sizeof(lineData));
                    lineDataLen += lineLen;
                }
            }

            if (lineDataLen > 0) {
                // Build authorization header
                char authHeader[256];
                snprintf(authHeader, sizeof(authHeader), "Token %s", config.influxdbToken);

                // Configure request options
                HttpRequestOptions options;
                options.contentType = content_text_plain;
                options.skipCertValidation = true;
                options.authHeader = authHeader;
                options.timeoutMs = 6000;

                // Send the data
                int16_t httpCode = 0;
                sendResult sendRes = http_request(writeURL, httpMethod::HTTP_POST, lineData, nullptr, 0, options, &httpCode);

                if (sendRes == sendResult::success) {
                    Log.notice("Completed send to InfluxDB.\r\n");
                } else {
                    Log.error("Error sending to InfluxDB\r\n");
                    result = false;
                }
                setTargetStatus(TARGET_INFLUXDB, httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
            } else {
                Log.verbose("No Tilt data to send to InfluxDB.\r\n");
            }
        }

        startTimer(influxdbTimer, backoffDelay(TARGET_INFLUXDB, config.influxdbPushEvery)); // Set up subsequent send to InfluxDB
    }
    return result;
}

