#include <cstring>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <nvs.h>
#include <thorlog.h>

#include "sender_health.h"
#include "sendData.h"   // SendTargetID, sendTargetNames
#include "jsonconfig.h"
#include "wifi_setup.h"
#include "tilt/tiltScanner.h"
#include "uptime.h"

SenderHealthMonitor sender_health;

#define RECOVERY_RECORD_MAGIC 0x54425231u   // 'TBR1'
#define NVS_RECOVERY_NAMESPACE "tbrecov"
#define NVS_RECOVERY_KEY       "last"

// Survives esp_restart() and watchdog resets; lost on a power cycle. Deliberately
// RTC_NOINIT so no flash write is needed on the hot path (spec section 25).
static RTC_NOINIT_ATTR RecoveryRecord rtc_recovery_record;
static RTC_NOINIT_ATTR uint32_t rtc_boot_count;

// Grace period after boot during which recovery never fires - WiFi association,
// the first BLE scan and the initial staggered sends all happen in here.
#define RECOVERY_GRACE_SEC 180

// BLE must have been heard from this recently for "BLE is alive" to hold.
#define BLE_ALIVE_WINDOW_MS 90000

uint32_t sh_millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// All ages are computed as unsigned differences, which stay correct across the
// ~49.7 day wrap of a uint32_t millisecond counter.
static inline uint32_t age_since(uint32_t then) {
    if (then == 0)
        return 0;
    return sh_millis() - then;
}

void SenderHealthMonitor::init() {
    if (m_lock == nullptr) {
        // Deliberately a binary semaphore rather than xSemaphoreCreateMutex(). The health
        // monitor may need to release a lock it does not own in order to recover a wedged
        // sender, and giving a priority-inheritance mutex from a non-owner task is illegal.
        m_lock = xSemaphoreCreateBinary();
        if (m_lock != nullptr)
            xSemaphoreGive(m_lock);   // created empty; start in the "free" state
    }
    m_h.lastHeartbeatMs = sh_millis();
}

void SenderHealthMonitor::heartbeat() {
    m_h.lastHeartbeatMs = sh_millis();
}

void SenderHealthMonitor::noteTargetResult(uint8_t targetId, bool success) {
    const uint32_t now = sh_millis();

    if (success) {
        m_h.lastAnySuccess = now;

        if (targetId == TARGET_GOOGLE_SHEETS)
            m_h.lastGoogleSuccess = now;
        else if (targetId == TARGET_FERMENTRACK || targetId == TARGET_LEGACY_FERMENTRACK)
            m_h.lastFermentrackSuccess = now;
    }

    // The per-target failure counters live on dataSendHandler::targetStatus[]. A single
    // shared counter was reset by any healthy target, which hid a target failing on every
    // cycle - exactly the case this fork needs to surface.
}

void SenderHealthMonitor::noteRequestStart() {
    // Only meaningful while a target holds the sender. Guards against any future caller
    // of http_request() that runs outside the outbound path (e.g. the web server).
    if (isHeld())
        m_h.requestStartedMs = sh_millis();
}

void SenderHealthMonitor::noteRequestEnd() {
    if (isHeld())
        m_h.requestStartedMs = 0;
}

bool SenderHealthMonitor::tryAcquire(uint8_t targetId, uint32_t timeoutMs) {
    // Fail open before init() so nothing can deadlock during early boot.
    if (m_lock == nullptr)
        return true;

    if (xSemaphoreTake(m_lock, 0) != pdTRUE)
        return false;

    m_h.state = SenderState::SENDING;
    m_h.currentTarget = (int8_t)targetId;
    m_h.lockAcquiredMs = sh_millis();
    m_h.requestStartedMs = 0;
    m_currentTimeoutMs = timeoutMs;
    return true;
}

void SenderHealthMonitor::release() {
    m_h.state = SenderState::IDLE;
    m_h.currentTarget = -1;
    m_h.lockAcquiredMs = 0;
    m_h.requestStartedMs = 0;
    m_currentTimeoutMs = 0;

    if (m_lock != nullptr)
        xSemaphoreGive(m_lock);
}

uint32_t SenderHealthMonitor::heartbeatAgeMs() const { return age_since(m_h.lastHeartbeatMs); }
uint32_t SenderHealthMonitor::lockAgeMs() const      { return age_since(m_h.lockAcquiredMs); }
uint32_t SenderHealthMonitor::requestAgeMs() const   { return age_since(m_h.requestStartedMs); }

static const char *senderStateName(SenderState s) {
    switch (s) {
        case SenderState::SENDING: return "SENDING";
        case SenderState::STALE:   return "STALE";
        default:                   return "IDLE";
    }
}

void SenderHealthMonitor::to_json(JsonDocument &doc) const {
    doc["state"] = senderStateName(m_h.state);

    if (m_h.currentTarget >= 0 && m_h.currentTarget < TARGET_COUNT)
        doc["currentTarget"] = sendTargetNames[m_h.currentTarget];
    else
        doc["currentTarget"] = nullptr;

    doc["heartbeatAgeSec"] = heartbeatAgeMs() / 1000;
    doc["lockHeld"] = isHeld();
    doc["lockAgeSec"] = lockAgeMs() / 1000;
    doc["requestAgeSec"] = requestAgeMs() / 1000;

    // Ages rather than absolute times - the UI has no shared clock with the device.
    if (m_h.lastGoogleSuccess != 0)
        doc["lastGoogleSuccessAgeSec"] = age_since(m_h.lastGoogleSuccess) / 1000;
    else
        doc["lastGoogleSuccessAgeSec"] = nullptr;

    if (m_h.lastFermentrackSuccess != 0)
        doc["lastFermentrackSuccessAgeSec"] = age_since(m_h.lastFermentrackSuccess) / 1000;
    else
        doc["lastFermentrackSuccessAgeSec"] = nullptr;

    if (m_h.lastAnySuccess != 0)
        doc["lastAnySuccessAgeSec"] = age_since(m_h.lastAnySuccess) / 1000;
    else
        doc["lastAnySuccessAgeSec"] = nullptr;

    // Worst case across all targets, so one persistently failing target is visible even
    // while others succeed. Per-target detail follows.
    uint16_t worstFailures = 0;
    JsonObject perTarget = doc["targetFailures"].to<JsonObject>();
    for (uint8_t i = 0; i < TARGET_COUNT; i++) {
        const uint16_t f = data_sender.targetStatus[i].consecutiveFailures;
        if (f > 0)
            perTarget[sendTargetNames[i]] = f;
        if (f > worstFailures)
            worstFailures = f;
    }

    doc["consecutiveSendFailures"] = worstFailures;
    doc["staleEvents"] = m_h.staleEvents;

    // Non-zero means wifi_cfg_is_connected() claimed "down" while the STA interface was
    // demonstrably up with a lease. That is the failure mode this fork was built to catch.
    doc["wifiFlagDisagreements"] = wifi_flag_disagreements();

    if (m_lastRecovery.reason != RECOVERY_NONE) {
        JsonObject rec = doc["lastRecovery"].to<JsonObject>();
        switch (m_lastRecovery.reason) {
            case RECOVERY_SENDER_HEARTBEAT_STALE: rec["reason"] = "sender_heartbeat_stale"; break;
            case RECOVERY_SENDER_LOCK_STUCK:      rec["reason"] = "sender_lock_stuck";      break;
            default:                              rec["reason"] = "unknown";                break;
        }
        rec["heartbeatAgeMs"] = m_lastRecovery.heartbeatAgeMs;
        rec["lockAgeMs"] = m_lastRecovery.lockAgeMs;
        rec["uptimeSecAtReboot"] = m_lastRecovery.uptimeSecAtReboot;
        rec["bootCount"] = m_lastRecovery.bootCount;

        if (m_lastRecovery.currentTarget >= 0 && m_lastRecovery.currentTarget < TARGET_COUNT)
            rec["target"] = sendTargetNames[m_lastRecovery.currentTarget];
        else
            rec["target"] = nullptr;
    } else {
        doc["lastRecovery"] = nullptr;
    }
}

//=============================================================================
// Recovery
//=============================================================================

void SenderHealthMonitor::loadRecoveryRecord() {
    // rtc_boot_count is RTC_NOINIT: garbage after a power cycle, preserved across a
    // software reset. Reset it whenever the reset reason says power-on/brownout.
    const esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT || rr == ESP_RST_UNKNOWN) {
        rtc_boot_count = 0;
        memset(&rtc_recovery_record, 0, sizeof(rtc_recovery_record));
    }
    rtc_boot_count++;

    if (rtc_recovery_record.magic == RECOVERY_RECORD_MAGIC) {
        m_lastRecovery = rtc_recovery_record;
        // Consume it so the UI reports it for this boot only.
        rtc_recovery_record.magic = 0;
        rtc_recovery_record.reason = RECOVERY_NONE;

        // The durable copy has now been reported too. Without this it would be re-read on
        // every later boot and the UI would show the same recovery for ever.
        markStoredRecoverySurfaced();

        Log.warning("Previous boot ended in sender recovery: reason %u, heartbeat age %u ms, target %d\r\n",
                    (unsigned)m_lastRecovery.reason,
                    (unsigned)m_lastRecovery.heartbeatAgeMs,
                    (int)m_lastRecovery.currentTarget);
        return;
    }

    /*
     * Nothing in RTC memory (power cycle). Fall back to the NVS copy, which survives power
     * loss so the cause is still visible after the user pulls the plug - but only until it
     * has been shown once. `surfaced` is what stops it becoming permanent.
     *
     * A blob written before `surfaced` existed is a different size, so the length check
     * below rejects it and any recovery recorded by older firmware is quietly dropped.
     * That is the intended migration: those records are historical and have, by
     * definition, already been on screen.
     */
    nvs_handle_t h;
    if (nvs_open(NVS_RECOVERY_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        RecoveryRecord stored{};
        size_t len = sizeof(stored);
        if (nvs_get_blob(h, NVS_RECOVERY_KEY, &stored, &len) == ESP_OK &&
            len == sizeof(stored) && stored.magic == RECOVERY_RECORD_MAGIC &&
            stored.surfaced == 0) {
            m_lastRecovery = stored;
            markStoredRecoverySurfaced();
        }
        nvs_close(h);
    }
}

/*
 * Mark the persisted recovery as reported, leaving everything else about it intact so the
 * record is still there to be read by anything that wants the history.
 *
 * Read-modify-write rather than an erase: the event itself is worth keeping, it just must
 * not be presented as news twice.
 */
void SenderHealthMonitor::markStoredRecoverySurfaced() {
    nvs_handle_t h;
    if (nvs_open(NVS_RECOVERY_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;

    RecoveryRecord stored{};
    size_t len = sizeof(stored);
    if (nvs_get_blob(h, NVS_RECOVERY_KEY, &stored, &len) == ESP_OK &&
        len == sizeof(stored) && stored.magic == RECOVERY_RECORD_MAGIC &&
        stored.surfaced == 0) {
        stored.surfaced = 1;
        nvs_set_blob(h, NVS_RECOVERY_KEY, &stored, sizeof(stored));
        nvs_commit(h);
    }
    nvs_close(h);
}

void SenderHealthMonitor::persistRecoveryRecord(RecoveryReason reason) {
    RecoveryRecord rec{};
    rec.magic = RECOVERY_RECORD_MAGIC;
    rec.bootCount = rtc_boot_count;
    rec.reason = (uint32_t)reason;
    rec.heartbeatAgeMs = heartbeatAgeMs();
    rec.lockAgeMs = lockAgeMs();
    rec.currentTarget = m_h.currentTarget;
    // Total uptime, not the 0..59 seconds component this used to record.
    rec.uptimeSecAtReboot = uptimeTotalSeconds();

    rtc_recovery_record = rec;

    // One NVS write per recovery event. Rare by construction, so no wear concern.
    nvs_handle_t h;
    if (nvs_open(NVS_RECOVERY_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_RECOVERY_KEY, &rec, sizeof(rec));
        nvs_commit(h);
        nvs_close(h);
    }
}

void SenderHealthMonitor::forceRelease(const char *reason) {
    Log.error("Sender lock force-released (%s) after %u ms held by target %d.\r\n",
              reason ? reason : "unknown", (unsigned)lockAgeMs(), (int)m_h.currentTarget);

    m_h.currentTarget = -1;
    m_h.lockAcquiredMs = 0;
    m_h.requestStartedMs = 0;
    m_currentTimeoutMs = 0;

    // Legal because m_lock is a binary semaphore, not a priority-inheritance mutex:
    // a task other than the taker is permitted to give it.
    if (m_lock != nullptr)
        xSemaphoreGive(m_lock);
}

bool SenderHealthMonitor::checkForStalledRequest() {
    if (!isHeld())
        return false;

    // 3x the target's own budget, floored at 30 s, so a legitimately slow endpoint
    // (Google Scripts at 10 s) is never flagged.
    uint32_t budget = m_currentTimeoutMs * 3;
    if (budget < 30000)
        budget = 30000;

    const uint32_t held = lockAgeMs();
    if (held <= budget)
        return false;

    m_h.state = SenderState::STALE;
    m_h.staleEvents++;

    Log.error("Sender STALE: target %d held the lock for %u ms (budget %u ms), request age %u ms, free heap %u.\r\n",
              (int)m_h.currentTarget, (unsigned)held, (unsigned)budget,
              (unsigned)requestAgeMs(), (unsigned)esp_get_free_heap_size());

    // Attempt safe cleanup. The wedged http_request() is still running on loopTask; this
    // only frees the lock so other targets can proceed. Escalation to a restart is
    // checkForRebootCondition()'s decision, based on the heartbeat.
    forceRelease("stale_lock");
    return true;
}

void SenderHealthMonitor::checkForRebootCondition() {
    if (!config.senderRecoveryEnabled)
        return;

    /*
     * Never during boot: association, the first scan and staggered first sends live here.
     *
     * This used uptimeSeconds(), which returns the SECONDS COMPONENT (0..59) rather than
     * total uptime - so it could never reach RECOVERY_GRACE_SEC (180) and this function
     * returned early on every call. The recovery reboot, the whole point of this monitor,
     * has never been able to fire.
     */
    if (uptimeTotalSeconds() < RECOVERY_GRACE_SEC)
        return;

    // Condition 1: the outbound loop has stopped being serviced.
    const uint32_t staleMs = (uint32_t)config.senderStaleRebootSec * 1000u;
    if (heartbeatAgeMs() <= staleMs)
        return;

    // Condition 2: BLE is still receiving. If BLE is also dead the problem is broader
    // than the sender, and a targeted sender restart would be the wrong diagnosis.
    const uint32_t lastAdvert = g_last_tilt_advert_ms.load(std::memory_order_relaxed);
    if (lastAdvert == 0 || (sh_millis() - lastAdvert) > BLE_ALIVE_WINDOW_MS) {
        Log.warning("Sender heartbeat stale (%u ms) but BLE is quiet too; not treating this as a sender fault.\r\n",
                    (unsigned)heartbeatAgeMs());
        return;
    }

    // Condition 3: the network is usable. A genuine WiFi outage must not reboot the
    // device - the queue is supposed to accumulate through it.
    if (!network_is_usable())
        return;

    triggerRecovery(RECOVERY_SENDER_HEARTBEAT_STALE);
}

void SenderHealthMonitor::triggerRecovery(RecoveryReason reason) {
    persistRecoveryRecord(reason);

    Log.error("Sender recovery: restarting. reason=%u heartbeatAge=%u ms lockAge=%u ms target=%d\r\n",
              (unsigned)reason, (unsigned)heartbeatAgeMs(), (unsigned)lockAgeMs(),
              (int)m_h.currentTarget);

    // Deliberately no tilt_scanner.wait_until_scan_complete() here, unlike loop()'s
    // restart path: that spins until the scanner finishes and would hang the monitor
    // if BLE were the wedged component.
    vTaskDelay(pdMS_TO_TICKS(250));   // let the log drain
    esp_restart();
}

static void senderMonitorTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        sender_health.checkForStalledRequest();
        sender_health.checkForRebootCondition();
    }
}

void SenderHealthMonitor::startMonitorTask() {
    if (m_monitorTask != nullptr)
        return;

    // Priority above loopTask (1) so a spinning loop cannot starve the monitor, and
    // pinned to core 0 (loopTask runs on core 1) so a fully blocked core 1 cannot either.
    xTaskCreatePinnedToCore(senderMonitorTask, "senderMon", 3072, nullptr, 2, &m_monitorTask, 0);
}
