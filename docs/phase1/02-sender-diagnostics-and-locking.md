# Stages 3–4: Sender diagnostics, then sender locking

Spec §17, §18. Do diagnostics first (stage 3) so the lock replacement (stage 4) has somewhere to
report into, and so a build exists that only *observes* before anything changes behaviour.

## New files

`src/sender_health.h`, `src/sender_health.cpp`

## Stage 3 — diagnostics only

### `src/sender_health.h`

```cpp
#ifndef TILTBRIDGE_SENDER_HEALTH_H
#define TILTBRIDGE_SENDER_HEALTH_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>

enum class SenderState : uint8_t {
    IDLE = 0,
    SENDING,
    STALE,          // request/lock outlived its timeout; cleanup attempted
};

// §18 required fields. Times are milliseconds from esp_timer_get_time()/1000,
// 0 meaning "never". Written only from the outbound sender task.
struct SenderHealth {
    SenderState state          = SenderState::IDLE;
    int8_t   currentTarget     = -1;    // SendTargetID, -1 = none
    uint32_t lastHeartbeatMs   = 0;     // proves process() keeps being entered
    uint32_t lockAcquiredMs    = 0;     // 0 = free
    uint32_t requestStartedMs  = 0;     // 0 = no request in flight
    uint32_t lastGoogleSuccess = 0;
    uint32_t lastFermentrackSuccess = 0;
    uint16_t consecutiveSendFailures = 0;
    uint32_t staleEvents       = 0;     // times the monitor declared STALE
};

class SenderHealthMonitor {
public:
    void init();                                    // create mutex; must run before data_sender.init()

    // --- called only from the outbound sender path ---
    void heartbeat();                               // top of dataSendHandler::process()
    void beginTarget(uint8_t targetId, uint32_t timeoutMs);
    void endTarget(bool success);
    void noteRequestStart();
    void noteRequestEnd();

    // --- observers, safe from any task ---
    uint32_t heartbeatAgeMs() const;
    uint32_t lockAgeMs() const;
    uint32_t requestAgeMs() const;
    void to_json(JsonDocument &doc) const;          // feeds /api/sender/

    // --- lock, §17 ---
    bool tryAcquire(uint8_t targetId, uint32_t timeoutMs);   // non-blocking; false = busy, skip
    void release();
    bool isHeld() const;
    void forceRelease(const char *reason);          // stage 5 recovery only

    SenderHealth snapshot() const;

private:
    SemaphoreHandle_t m_lock = nullptr;             // binary mutex guarding the sender
    SenderHealth      m_h{};
    uint32_t          m_currentTimeoutMs = 0;
};

extern SenderHealthMonitor sender_health;

// millis() equivalent — the tree already open-codes this in tiltHydrometer.cpp:12
uint32_t sh_millis();

#endif
```

### `src/sender_health.cpp` notes

- `sh_millis()` = `(uint32_t)(esp_timer_get_time() / 1000ULL)`. All ages computed as
  `now - then` on `uint32_t`, which is wrap-safe for differences (49.7-day wrap).
- `init()`: `m_lock = xSemaphoreCreateMutex();` and `m_h.lastHeartbeatMs = sh_millis();`
  (so age is 0, not "49 days", before the first loop pass).
- `heartbeat()` sets `m_h.lastHeartbeatMs = sh_millis()` only. Nothing else. §18 forbids BLE or
  web-server tasks touching it — enforce by convention plus a comment; the only caller is
  `dataSendHandler::process()`.
- `to_json()` emits ages, not absolute times, because the UI has no shared clock:
  ```json
  {
    "state": "IDLE",              // IDLE | SENDING | STALE
    "currentTarget": null,        // or "google_sheets" (reuse sendTargetNames[] from http_server.cpp:130)
    "heartbeatAgeSec": 1,
    "lockHeld": false,
    "lockAgeSec": 0,
    "requestAgeSec": 0,
    "lastGoogleSuccessAgeSec": 120,     // null when never
    "lastFermentrackSuccessAgeSec": 118,
    "consecutiveSendFailures": 0,
    "staleEvents": 0
  }
  ```
  `sendTargetNames[]` is currently `static` in `http_server.cpp:130` — move it to `sendData.h`
  as `extern const char* const sendTargetNames[TARGET_COUNT];` defined in `sendData.cpp`, so both
  the errors endpoint and this one share it. Keep the existing eleven strings and order
  identical; `SendTargetErrorStore.js` depends on those names.

### Wiring for stage 3

1. `src/sendData.cpp` — `dataSendHandler::process()`:
   ```cpp
   void dataSendHandler::process() {
       sender_health.heartbeat();          // FIRST, unconditionally
       if (!network_is_usable()) return;   // stage 5 introduces network_is_usable(); for stage 3 keep is_wifi_connected()
       ...
   }
   ```
   The heartbeat must be *outside* the Wi-Fi gate. It proves the loop is alive; a separate
   condition proves work is possible. Stage 5's monitor needs both.
2. `src/main.cpp` `setup()` — call `sender_health.init();` immediately before `data_sender.init();`.
3. Success recording: in each `send_to_*`, after the existing `setTargetStatus(...)` call, add
   `sender_health.endTarget(<that call's error> == SEND_OK)`. For Google use
   `TARGET_GOOGLE_SHEETS` and for both Fermentrack variants set `lastFermentrackSuccess`.
   Simplest: do it centrally inside `dataSendHandler::setTargetStatus()`
   (`sendData.cpp:26`) — it already receives target + error and is called by every sender:
   ```cpp
   void dataSendHandler::setTargetStatus(SendTargetID target, SendError error) {
       if (target >= TARGET_COUNT) return;
       targetStatus[target].lastError = error;
       targetStatus[target].lastAttemptTime = (uint32_t)uptimeSeconds(true);
       sender_health.noteTargetResult(target, error == SEND_OK);   // new
   }
   ```
   Add `noteTargetResult(uint8_t, bool)` to the monitor: updates
   `lastGoogleSuccess` / `lastFermentrackSuccess` and bumps or clears
   `consecutiveSendFailures`. This is the lowest-touch wiring and covers all eleven targets.
   MQTT does not always go through `setTargetStatus` — check `targets/mqtt.cpp` and add the call
   if missing.
4. `src/CMakeLists.txt` — add `sender_health.cpp` to `SRCS`. **Check how sources are listed**
   (it may glob); `src/CMakeLists.txt` is only 199 bytes, read it before editing.
5. New GET endpoint `/api/sender/` in `http_server.cpp`:
   - add `static void sender_json(JsonDocument &doc) { sender_health.to_json(doc); }`
   - `MAKE_GET_HANDLER(handle_api_sender, sender_json)`
   - add `{"/api/sender/", handle_api_sender}` to `get_endpoints[]` in
     `registerJsonGetHandlers()`.

**Build here.** Behaviour is unchanged except a new read-only endpoint.

## Stage 4 — replace the boolean lock

### Semantics to preserve

`!send_lock` means *skip this pass, retry next loop iteration*. The mutex must be taken with a
zero tick timeout so a busy sender is skipped, never blocked on.

### Mechanical transformation

Every one of the eleven sites in `01-findings-identity-and-lock-audit.md` §2 becomes:

```cpp
// before
if (send_gSheets && !send_lock) {
    send_gSheets = false;
    send_lock = true;
    ...
    startTimer(gSheetsTimer, GSCRIPTS_DELAY);
    send_lock = false;
}

// after
if (send_gSheets) {
    SenderLock lock(TARGET_GOOGLE_SHEETS, /*timeoutMs=*/10000);
    if (!lock)                       // another target holds the sender; retry next pass
        return result;
    send_gSheets = false;
    ...
    startTimer(gSheetsTimer, GSCRIPTS_DELAY);
}   // lock releases here on every path, including early return and exception
```

### RAII guard (add to `sender_health.h`)

```cpp
class SenderLock {
public:
    SenderLock(uint8_t targetId, uint32_t timeoutMs)
        : m_held(sender_health.tryAcquire(targetId, timeoutMs)) {}
    ~SenderLock() { if (m_held) sender_health.release(); }
    explicit operator bool() const { return m_held; }
    SenderLock(const SenderLock&) = delete;
    SenderLock& operator=(const SenderLock&) = delete;
private:
    bool m_held;
};
```

`tryAcquire()`:
```cpp
bool SenderHealthMonitor::tryAcquire(uint8_t targetId, uint32_t timeoutMs) {
    if (m_lock == nullptr) return true;              // pre-init: fail open, never deadlock boot
    if (xSemaphoreTake(m_lock, 0) != pdTRUE) return false;
    m_h.state = SenderState::SENDING;
    m_h.currentTarget = (int8_t)targetId;
    m_h.lockAcquiredMs = sh_millis();
    m_currentTimeoutMs = timeoutMs;
    return true;
}
```
`release()` clears `state`, `currentTarget = -1`, `lockAcquiredMs = 0`, `requestStartedMs = 0`,
then `xSemaphoreGive(m_lock)`.

The `timeoutMs` argument is the *configured HTTP timeout for that target* and is what stage 5
compares against ("substantially longer than its configured HTTP timeout" — §19). Per-target
values, from the existing code: Google 10000 (`sendData.cpp:528`), InfluxDB 6000
(`sendData.cpp:628`), everything else the `HttpRequestOptions` default 6000
(`send_json_str.h:83`). Fermentrack 2 makes **three** sequential requests
(`fermentrack_2.cpp:114-116`), so pass `3 * 6000`.

### Per-site checklist (do all eleven; delete `bool send_lock` last)

- [ ] `sendData.cpp:159` Brewer's Friend — `TARGET_BREWERS_FRIEND`, 6000
- [ ] `sendData.cpp:180` Brewfather — `TARGET_BREWFATHER`, 6000
- [ ] `sendData.cpp:202` User target — `TARGET_USER_TARGET`, 6000
- [ ] `sendData.cpp:316` Grainfather — `TARGET_GRAINFATHER`, 6000
- [ ] `sendData.cpp:370` taplist.io — `TARGET_TAPLISTIO`, 6000. Note this one already returns
      early *before* claiming; keep the `strlen(config.taplistioURL) <= 10` and `!send_taplistio`
      guards above the `SenderLock`, and keep the explicit `xTimerStop(taplistioTimer, 0)`.
- [ ] `sendData.cpp:426` Brewstatus — `TARGET_BREW_STATUS`, 6000
- [ ] `sendData.cpp:479` Google Sheets — `TARGET_GOOGLE_SHEETS`, 10000
- [ ] `sendData.cpp:573` InfluxDB — `TARGET_INFLUXDB`, 6000
- [ ] `targets/legacy_fermentrack.cpp:13` — `TARGET_LEGACY_FERMENTRACK`, 6000
- [ ] `targets/fermentrack_2.cpp:106` — `TARGET_FERMENTRACK`, 18000
- [ ] `targets/mqtt.cpp:155` — `TARGET_MQTT`, 6000
- [ ] `src/sendData.h:134` — delete `bool send_lock = false;`
- [ ] `grep -rn send_lock src/` returns nothing

Three of these sites live in other translation units and referenced the private member because
they define `dataSendHandler` methods — `SenderLock` is a free class, so they just need
`#include "sender_health.h"`.

### Multi-request senders and `noteRequestStart/End`

Wrap the actual `http_request()` calls so `requestStartedMs` reflects the in-flight request, not
the whole target pass. Cleanest: do it inside `http_request()` itself
(`src/targets/send_json_str.cpp:161`) — one place, covers every caller including future queue
drains:

```cpp
sender_health.noteRequestStart();
esp_err_t err = esp_http_client_perform(client);
sender_health.noteRequestEnd();
```
Guard against the web-server task ever calling `http_request` (it does not today) by making
`noteRequestStart/End` no-ops unless the lock is currently held.

### Also fix in this stage

`sendData.cpp:539` null-`doclongurl` crash (see `01-findings-…` end):

```cpp
const char *docurl = retval["doclongurl"].as<const char*>();
if (docurl != nullptr && strcmp(config.gsheets_config[th.m_color].link, docurl) != 0) {
    strlcpy(config.gsheets_config[th.m_color].link, docurl, sizeof(config.gsheets_config[th.m_color].link));
    config.save();
}
```
Also check `deserializeJson(retval, response)`'s return and skip the block on error.

**Build here.**
