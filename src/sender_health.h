#ifndef TILTBRIDGE_SENDER_HEALTH_H
#define TILTBRIDGE_SENDER_HEALTH_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <ArduinoJson.h>

// Milliseconds since boot. Matches the millis() helper open-coded in tiltHydrometer.cpp.
uint32_t sh_millis();

enum class SenderState : uint8_t {
    IDLE = 0,
    SENDING,
    STALE,      // A request or the lock outlived its budget; cleanup was attempted.
};

// Why the health monitor decided to restart the device. Persisted across the
// restart so the cause is visible after the fact.
enum RecoveryReason : uint32_t {
    RECOVERY_NONE = 0,
    RECOVERY_SENDER_HEARTBEAT_STALE,
    RECOVERY_SENDER_LOCK_STUCK,
};

struct RecoveryRecord {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t reason;
    uint32_t heartbeatAgeMs;
    uint32_t lockAgeMs;
    int32_t  currentTarget;
    uint32_t uptimeSecAtReboot;
};

// Snapshot of everything the sender exposes. Times are ms since boot; 0 means "never".
struct SenderHealth {
    SenderState state = SenderState::IDLE;
    int8_t   currentTarget = -1;            // SendTargetID, -1 = none
    uint32_t lastHeartbeatMs = 0;           // proves the outbound loop keeps being entered
    uint32_t lockAcquiredMs = 0;            // 0 = lock free
    uint32_t requestStartedMs = 0;          // 0 = no HTTP request in flight
    uint32_t lastGoogleSuccess = 0;
    uint32_t lastFermentrackSuccess = 0;
    uint32_t lastAnySuccess = 0;
    uint32_t staleEvents = 0;
};

/**
 * @brief Tracks whether the outbound sender is still making progress, and owns the sender lock.
 *
 * The heartbeat is deliberately written from exactly one place - the top of
 * dataSendHandler::process(). BLE and web-server tasks must never touch it, otherwise it stops
 * proving anything about the outbound path.
 */
class SenderHealthMonitor {
public:
    void init();                // create the lock; call before data_sender.init()

    // --- written only from the outbound sender path ---
    void heartbeat();
    void noteTargetResult(uint8_t targetId, bool success);
    void noteRequestStart();
    void noteRequestEnd();

    // --- lock (replaces the old bool send_lock) ---
    bool tryAcquire(uint8_t targetId, uint32_t timeoutMs);  // non-blocking; false = busy, skip
    void release();
    bool isHeld() const { return m_h.lockAcquiredMs != 0; }

    // --- observers, safe from any task ---
    uint32_t heartbeatAgeMs() const;
    uint32_t lockAgeMs() const;
    uint32_t requestAgeMs() const;
    SenderHealth snapshot() const { return m_h; }
    void to_json(JsonDocument &doc) const;

    // --- recovery, run on the independent monitor task (never on the sender) ---
    void startMonitorTask();
    bool checkForStalledRequest();      // true when it declared a stall this pass
    void checkForRebootCondition();
    void forceRelease(const char *reason);

    // Recovery record captured before the previous restart, if any. `reason` is
    // RECOVERY_NONE when the last boot was not one of ours.
    const RecoveryRecord &lastRecovery() const { return m_lastRecovery; }
    void loadRecoveryRecord();          // call once during setup()

#ifdef TB_DEBUG_FREEZE
    // Test hook (see docs/phase1/10-acceptance-tests.md T6). When set, process()
    // returns without calling heartbeat(), simulating a wedged outbound loop.
    volatile bool debugFreeze = false;
#endif

private:
    void persistRecoveryRecord(RecoveryReason reason);
    void triggerRecovery(RecoveryReason reason);

    SemaphoreHandle_t m_lock = nullptr;
    SenderHealth m_h{};
    uint32_t m_currentTimeoutMs = 0;
    TaskHandle_t m_monitorTask = nullptr;
    RecoveryRecord m_lastRecovery{};
};

extern SenderHealthMonitor sender_health;

/**
 * @brief Scoped sender lock. Releases on every exit path, including early returns.
 *
 * Acquisition is non-blocking: a false result means another target holds the sender and this
 * one should be skipped and retried on the next pass. That preserves the semantics of the
 * boolean `send_lock` it replaces - the loop never blocks waiting for a peer.
 */
class SenderLock {
public:
    SenderLock(uint8_t targetId, uint32_t timeoutMs)
        : m_held(sender_health.tryAcquire(targetId, timeoutMs)) {}
    ~SenderLock() { if (m_held) sender_health.release(); }

    explicit operator bool() const { return m_held; }

    SenderLock(const SenderLock &) = delete;
    SenderLock &operator=(const SenderLock &) = delete;

private:
    bool m_held;
};

#endif // TILTBRIDGE_SENDER_HEALTH_H
