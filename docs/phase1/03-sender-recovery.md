# Stage 5: Sender-stale detection and automatic recovery

Spec §1, §19. This is the stage that actually addresses the reported field failure.

## Design constraint

The monitor must not run on the task it is watching. `data_sender.process()` runs on `loopTask`
(`main.cpp:200`), so the monitor gets its **own** FreeRTOS task. It must touch nothing that could
itself be wedged — no HTTP, no config writes on the hot path, no `send_lock`-guarded work.

## Part A — stop trusting a single Wi-Fi flag

This is the highest-value change in the whole phase (see `00-OVERVIEW.md`).

### `src/wifi_setup.h` / `.cpp`

```cpp
// True when the STA interface can actually carry traffic, independent of the
// esp_wifi_config library's internal connected flag. A stale-false flag there
// silently disabled every outbound target while BLE and the web UI kept working.
bool network_is_usable();
```

```cpp
bool network_is_usable() {
    if (wifi_cfg_is_connected())
        return true;

    // Library flag says no — verify against the netif directly before believing it.
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr || !esp_netif_is_netif_up(netif))
        return false;

    esp_netif_ip_info_t ip{};
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0)
        return false;

    // Netif is up with a lease but the manager disagrees: record the disagreement
    // and let sends proceed. http_request() fails safely if the link is really down.
    wifi_flag_disagreements++;
    return true;
}
```

Expose `wifi_flag_disagreements` (a `uint32_t`) through `/api/sender/` as
`wifiFlagDisagreements`. If the field is non-zero in the field, the diagnosis in
`00-OVERVIEW.md` is confirmed and the stale-flag theory is the root cause.

Then in `sendData.cpp`:
```cpp
void dataSendHandler::process() {
    sender_health.heartbeat();
    if (!network_is_usable()) return;
    ...
}
```

### Rate-limit the reconnect spin

`reconnectWiFi()` (`wifi_setup.cpp:249`) currently calls `wifi_cfg_connect(NULL)` on every
`loop()` iteration (~10 ms) whenever the flag is false. Throttle to one attempt per 10 s:

```cpp
void reconnectWiFi() {
    static uint32_t lastAttemptMs = 0;
    if (wifi_cfg_is_connected()) { lastAttemptMs = 0; return; }
    uint32_t now = sh_millis();
    if (lastAttemptMs != 0 && (now - lastAttemptMs) < 10000) return;
    lastAttemptMs = now;
    wifi_cfg_connect(NULL);
}
```

Also consider re-enabling the commented-out `WIFI_CFG_EVT_DISCONNECTED` subscription
(`wifi_setup.cpp:170`) so `wifi_was_disconnected` and the LCD state stay coherent. It was
presumably disabled for a reason — check `git log -S "WIFI_CFG_EVT_DISCONNECTED" --oneline` before
changing, and if the reason is unclear leave it commented and note it.

## Part B — stale request / stale lock cleanup

Add to `SenderHealthMonitor`:

```cpp
// Runs on the monitor task. Returns true if it declared a stall.
bool checkForStalledRequest();
```

Rule (§19): if the lock is held **and** `lockAgeMs > max(3 * m_currentTimeoutMs, 30000)`:

1. `m_h.state = SenderState::STALE; m_h.staleEvents++;`
2. Log target, lock age, request age, free heap at `Log.error` level.
3. Attempt safe cleanup: `forceRelease("stale_lock")` — `xSemaphoreGive` on the mutex plus reset
   of the tracked fields.
   **Caveat to write into the code comment:** force-releasing a FreeRTOS mutex from a task that
   does not own it is not legal for `xSemaphoreCreateMutex()` (priority-inheritance mutexes
   assert on non-owner gives). Use `xSemaphoreCreateBinary()` + an initial give instead, which
   permits cross-task gives, and give up priority inheritance (irrelevant here — one producer).
   Decide this in stage 4 and write the mutex creation accordingly.
4. Do **not** reboot on a stale lock alone. The wedged `http_request` is still on `loopTask`; if
   it eventually returns, the sender recovers. Escalation is Part C's job.

The 3× multiplier keeps normal slow-Google responses (10 s timeout) from being flagged.

## Part C — reboot when outbound processing stops progressing

### Conditions, all of which must hold (§19)

```
BLE is alive        : at least one configured, non-expired Tilt was updated in the last 90 s
Network is alive    : network_is_usable() == true
Sender is not alive : sender_health.heartbeatAgeMs() > SENDER_STALE_REBOOT_MS   (default 75000)
Grace period passed : uptime > 180 s   (never reboot during boot/first-connect)
```

Explicitly **not** a reboot condition (§19, last paragraph): repeated HTTP failures while the
heartbeat keeps advancing. `consecutiveSendFailures` may be in the hundreds during a real Google
outage; that must keep retrying normally and never reboot.

75 s sits inside the spec's 60–90 s window and above the worst-case legitimate blocking pass
(Fermentrack 2 = 3 requests × 6 s, plus Google 10 s = ~28 s, plus mDNS resolution). If field logs
show false positives, raise to 90 s rather than lowering the timeouts.

### BLE liveness signal

`tiltHydrometer::m_lastUpdate` is private and only exposed as
`j["lastReceived"] = (millis() - m_lastUpdate) / 1000` in `to_json()`. Add a cheap accessor
rather than reaching into the list from the monitor:

```cpp
// tiltScanner.h
uint32_t ms_since_last_tilt_update() const;   // UINT32_MAX when no tilts are known
```
Implement by iterating `m_tilt_devices` and taking the minimum of `millis() - m_lastUpdate`
(needs a public getter on `tiltHydrometer`, e.g. `uint32_t last_update_age_ms() const`).

Iterating the list from the monitor task races with `loopTask`/BLE-callback mutation
(`get_or_create_tilt` does `push_front`, `drop_expired_tilts` does `erase`). **Do not iterate
from the monitor task.** Instead have the BLE path publish a plain integer:

```cpp
// tiltScanner.cpp, at the end of load_tilt_from_advert_hex(), after th->set_values(...)
std::atomic<uint32_t> g_last_tilt_advert_ms{0};
g_last_tilt_advert_ms.store(sh_millis(), std::memory_order_relaxed);
```
The monitor reads that atomic. Simple, lock-free, and it is exactly the signal §19 describes
("BLE continues receiving active configured Tilts"). Filter to configured/enabled devices once
stage 6 lands — until then any Tilt advert counts.

### Recovery reason, persisted across the reboot

Must survive `esp_restart()` and be visible afterwards, without writing flash every few seconds
(§25). Two options:

- **RTC slow memory** (`RTC_NOINIT_ATTR`): survives software reset and watchdog, lost on power
  cycle. Zero flash wear. Preferred.
- NVS: survives power loss but costs a write per event. Acceptable because events are rare.

Do both, cheaply: keep the structured reason in RTC memory for the UI, and write one NVS blob
right before restarting (a reboot event is by definition infrequent).

```cpp
// sender_health.h
struct RecoveryRecord {
    uint32_t magic;                 // 0x54425231 'TBR1'
    uint32_t bootCount;
    uint32_t reason;                // enum RecoveryReason
    uint32_t heartbeatAgeMs;
    uint32_t lockAgeMs;
    int8_t   currentTarget;
    uint32_t uptimeSecAtReboot;
};
enum RecoveryReason : uint32_t {
    RECOVERY_NONE = 0,
    RECOVERY_SENDER_HEARTBEAT_STALE,
    RECOVERY_SENDER_LOCK_STUCK,
};
```

Read it in `setup()` (validate `magic`), expose through `/api/sender/` as `lastRecovery`, then
clear `reason` so the UI shows it once per boot. Surface it next to the existing reset-reason
panel (`ResetReasonStore.js` / `About.vue`) — that page already shows `esp_reset_reason()`, which
will read `ESP_RST_SW` for our restarts, so the extra detail is what makes it actionable.

### The monitor task

```cpp
// sender_health.cpp
static void senderMonitorTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        sender_health.checkForStalledRequest();
        sender_health.checkForRebootCondition();
    }
}

void SenderHealthMonitor::startMonitorTask() {
    xTaskCreatePinnedToCore(senderMonitorTask, "senderMon", 3072, nullptr,
                            2,          // above loopTask's priority 1
                            &m_monitorTask,
                            0);         // core 0, opposite loopTask's core 1
}
```

Priority 2 > `loopTask`'s 1 so the monitor still runs if `loopTask` spins; core 0 so a fully
blocked core 1 cannot starve it. 3072 bytes of stack is enough — no printf-heavy work beyond
`Log.error`, no JSON.

Call `sender_health.startMonitorTask()` at the end of `setup()` in `main.cpp`, after
`data_sender.init()`.

### Reboot path

```cpp
void SenderHealthMonitor::triggerRecovery(RecoveryReason reason) {
    persistRecoveryRecord(reason);          // RTC + NVS
    Log.error("Sender recovery: rebooting (reason %d, heartbeat age %u ms, target %d)\r\n",
              (int)reason, heartbeatAgeMs(), m_h.currentTarget);
    vTaskDelay(pdMS_TO_TICKS(250));         // let the log drain
    esp_restart();
}
```

Do **not** call `tilt_scanner.wait_until_scan_complete()` here the way `loop()` does
(`main.cpp:154`) — that spins until the scanner finishes and would deadlock the monitor if BLE is
the wedged part. Restart directly.

## Config knobs (add to `Config`, §22-adjacent but sender-scoped)

```cpp
bool     senderRecoveryEnabled = true;      // let a user disable auto-reboot
uint16_t senderStaleRebootSec  = 75;        // clamp 60..600 on load
```
Serialize as `senderRecoveryEnabled` / `senderStaleRebootSec`. Both default-safe on old configs
because `load_from_json` skips missing keys.

## Verification for this stage

Simulated freeze — pick one and gate it behind a build flag or a debug endpoint, not shipped
behaviour:

```cpp
// POST /api/actions/  {"action":"debugFreezeSender"}   (compile only when -D TB_DEBUG_FREEZE=1)
// sets a flag that makes dataSendHandler::process() return early WITHOUT calling heartbeat()
```
Expected: BLE keeps updating, web UI stays up, `/api/sender/` shows `heartbeatAgeSec` climbing,
and at ~75 s the device reboots with `lastRecovery.reason = SENDER_HEARTBEAT_STALE`.

**Build here.**
