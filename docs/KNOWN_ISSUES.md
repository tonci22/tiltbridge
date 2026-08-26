# Known issues and dead ends

Open problems found by reading the code and by soaking a live four-Tilt device
(ESP32-D0WD-V3, `esp32_headless`) for ~25 hours across three runs.

Each entry says what is **proven** and what is only **suspected**, because several
plausible-sounding theories in this file turned out to be wrong and were expensive to
re-derive. The last section exists so nobody re-investigates things that are not bugs.

---

## Firmware — open

### 1. Seven other targets still have the two bugs fixed for Google Sheets

`src/http_server.cpp`. `processGoogleSheetsSettings()` was fixed to (a) only queue an
immediate send when the URL or email actually changed, and (b) re-arm the send timer when
the push interval changes, measured from the last upload.

The same two faults remain in `processBrewersFriendSettings`, `processBrewfatherSettings`,
`processUserTargetSettings`, `processGrainfatherSettings`, `processBrewStatusSettings`,
`processTaplistioSettings`, `processMqttSettings` and `processInfluxdbSettings`: each calls
`startSendNowTimer(...)` gated only on credentials being non-empty, and none re-arms its
timer on an interval change.

**Proven.** Not fixed because every one of those targets is unconfigured on the test device,
so none could be verified on hardware.

### 2. One stray advertisement can switch a Standard Tilt to Pro, permanently

`src/tilt/tiltHydrometer.cpp:143-150`. `set_values()` flips `tilt_pro` on a *single* packet
with `i_grav >= 5000`, which changes the gravity scalar from 1000 to 10000 and resets the
smoothing filter. One corrupted advertisement makes a Standard Tilt log gravity wrong by a
factor of ten, indefinitely.

Never observed firing — the wildest real value seen was raw 1280, during a battery change.
Requiring two consecutive samples before changing model would close it for nothing.

### 3. `m_tilt_devices` is mutated from three tasks with no lock

`src/tilt/tiltScanner.cpp:230` (`push_front`, NimBLE host task), `:250` (`erase`, via
`drop_expired_tilts()`), reached from `tilt_to_json()` on the **httpd** task
(`src/http_server.cpp:88`, `GET /api/json/`) and from every `send_to_*` on **loopTask**.

The code comments acknowledge the hazard and say the monitor task must never touch the list,
but the HTTP handler does exactly that. Survived 530 polls at 60 s intervals without incident,
so it is latent rather than frequent — but polling `/api/json/` is actively exercising it.

### 4. FIXED — large stack frames on the 8 KB `loopTask`

Both call sites are off the stack.

- `src/targets/legacy_fermentrack.cpp` — `char tilt_data[TILT_ALL_DATA_SIZE + 128]` was 4,015
  bytes (477 * 8 + 71 + 128) and stayed live across `http_request()`, which adds its own frame
  plus `esp_http_client_perform()`'s underneath it: roughly 4.7 KB of the 8 KB in one call
  chain, on the task that also runs every other sender. Now the `measureJson` + heap + verify
  pattern from `fermentrack_2.cpp`, with the `JsonDocument`s scoped so they are destroyed
  before the request goes out. An allocation failure or a short serialise logs and marks the
  cycle failed instead of sending a truncated body.
- `src/http_calibration.cpp` — `char victims[MAX_DEVICE_CONFIGS + TILT_COLORS][...]` was 16
  rows of 272 bytes, 4,352 of the same stack. Now one 272-byte buffer, deleting one file per
  directory pass. Reopening the directory per file preserves the reason the collect-then-delete
  shape existed at all: removing entries while walking one is not guaranteed safe on LittleFS.

  **That array was also silently capping a factory reset at 16 files.** The `readdir` loop
  stopped once it was full, so a device with more calibration files than that kept the
  remainder across a reset that claims to erase everything. The same change fixes both.

**Verified on hardware** (`esp32_headless`):

- Legacy Fermentrack pointed at a listener on the LAN: `Calling send to Legacy Fermentrack.`
  → `Completed send to Legacy Fermentrack.`, with the payload arriving as valid JSON carrying
  exactly `mdns_id` and `tilts`. **Caveat**: no Tilts were in range, so the body was 37 bytes.
  The success and free paths are proven; the large-payload case that motivated the fix is not,
  and neither are the allocation-failure and truncation branches.
- 22 calibration files created over the API, then a factory reset: `Deleted 22 calibration
  point files.` and a clean restart. Twenty-two is deliberately six past the old cap, so this
  covers the silent-16 bug as well as the stack.

An `ESP_ERR_HTTP_INCOMPLETE_DATA` seen on the first two sends was the test listener replying
without a `Content-Length`, not the firmware — worth knowing before anyone re-runs this.

### 5. `buildResolvedUrl` can write past its buffer

`src/targets/send_json_str.cpp:128-148` accumulates `written += snprintf(buf + written,
bufferSize - written, ...)`. `snprintf` returns what it *would* have written, so once
`written >= bufferSize` the pointer is past the end and the size underflows to a huge
`size_t`. `resolvedUrl` is 512 B while `ParsedUrl::path` alone is 512.

Reachable only on the `isMDNS()` branch — an mDNS target URL with a long path or query.

### 6. `weeks_on_battery` accepts any byte

`src/tilt/tiltHydrometer.cpp:181` assigns the raw tx-power byte with no range check. Observed
flapping 0 → 49 → 210 while a Tilt was handled during a battery change. The field's own
initialiser at line 63 still says "Not currently implemented - for future use".

### 7. Dangling stack pointer in `idf_json_send_response`

`src/idf_json_utils.cpp:76-79` passes a block-scoped `char status_str[16]` to
`httpd_resp_set_status()`, which stores the pointer rather than copying; the buffer dies
before `httpd_resp_sendstr()` reads it. **Currently unreachable** — the only call site
(`http_server.cpp:850`) always uses the default 200. The sibling `idf_json_send_error` gets
this right. Landmine for the next caller.

### 8. FIXED — `uptimeSeconds()` returned 0..59, which silently disabled the recovery reboot

Kept here because of what it broke and how long it hid.

`uptimeSeconds()` returned the **seconds component of a d/h/m/s breakdown (0..59)**, not total
uptime, while reading like the latter. Three consumers used it as a total:

1. **`sender_health.cpp:308` — the worst one.** The recovery reboot's boot-grace guard was
   `if (uptimeSeconds(true) < RECOVERY_GRACE_SEC) return;` with `RECOVERY_GRACE_SEC` = 180.
   A value capped at 59 can never reach 180, so `checkForRebootCondition()` returned early on
   **every call** and the recovery reboot could never fire. That watchdog is the whole reason
   Phase 1 exists — it was built to fix a field failure whose symptom was "a reset fixes it" —
   and it had never been able to act. It also explains `staleEvents: 0` and
   `lastRecovery: null` across ~25 hours of soaking: healthy, but the mechanism could not have
   reported otherwise.
2. `sender_health.cpp:247` — `uptimeSecAtReboot` in the recovery record, reported through
   `/api/sender/`, was meaningless.
3. `sendData.cpp` — `SendTargetStatus::lastAttemptTime`, published as `/api/errors/`'s
   `last_attempt_at` and documented as "uptime seconds when last attempted", held a value that
   wrapped every minute.

Separately, `/api/uptime/` built its response from five separate accessor calls, each
re-sampling the clock, so the fields could straddle a rollover — observed reporting `2m59s`
and then `2m0s`, which makes a client differencing it against itself go backwards.

Fixed by deriving every field from one clock read (`uptimeSnapshot()`), adding
`uptimeTotalSeconds()` for elapsed-time arithmetic, and moving all three consumers onto it.
`/api/uptime/` now also returns `totalSeconds`.

**Verified on hardware.** Built with `-D TB_DEBUG_FREEZE=1`, waited past the grace period, and
triggered `debugFreezeSender`:

```
freeze at uptime 194 s
22:46:39  E: Sender recovery: restarting. reason=1 heartbeatAge=90668 ms lockAge=0 ms target=-1
22:46:40  rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
```

and after the restart, `/api/sender/`:

```json
"lastRecovery": { "reason": "sender_heartbeat_stale", "heartbeatAgeMs": 90662,
                  "uptimeSecAtReboot": 285, "bootCount": 1 }
```

90,668 ms is `senderStaleRebootSec` (90) exactly; 285 s is 194 + 90; `rst:0xc (SW_CPU_RESET)` is
a deliberate software reset rather than a crash or hardware watchdog; and the record survived
the restart. `uptimeSecAtReboot: 285` doubles as proof of the fix, since that field could
previously only have held 0..59.

Reproducing it: `PLATFORMIO_BUILD_FLAGS="-D TB_DEBUG_FREEZE=1" pio run -e esp32_headless
--target upload` (an env override, so `platformio.ini` stays clean), then
`POST /api/actions/ {"action":"debugFreezeSender"}`. **Reflash the normal build afterwards** and
confirm the hook is gone — that action must return HTTP 400 in a production build.

### 9. FIXED — one recovery reboot showed its banner in the UI for ever

`src/sender_health.cpp`, `loadRecoveryRecord()`. The recovery record is stored twice, and
only one copy was ever consumed:

```cpp
if (rtc_recovery_record.magic == RECOVERY_RECORD_MAGIC) {
    m_lastRecovery = rtc_recovery_record;
    // Consume it so the UI reports it for this boot only.
    rtc_recovery_record.magic = 0;          // <- cleared
    ...
    return;
}
// Nothing in RTC memory (power cycle). Fall back to the NVS copy...
    m_lastRecovery = stored;                // <- never cleared
```

The RTC copy behaves exactly as its comment says. The NVS copy, added underneath for the
good reason that it survives a power cycle, had no matching step — so every later boot fell
through to it and re-reported the same event. A device that recovered once told you so for
the rest of its life, and the comment's promise of "this boot only" was quietly false.

Mistaken for a reboot loop, which is the real cost: the device was healthy
(`staleEvents: 0`, `bootCount` never past 1) and the banner said otherwise. The event being
reported was the deliberate `TB_DEBUG_FREEZE` verification in issue 8 — the reported
`heartbeatAgeMs: 90662`, `uptimeSecAtReboot: 285`, `bootCount: 1` are that test's numbers
exactly.

Fixed with a `surfaced` flag on the persisted record: reported while clear, then set and
written back, so the durable copy is shown on exactly one boot no matter which path finds
it and still survives a power cycle until it has actually been seen. Erasing the key
outright would have been simpler but breaks the case the key exists for — a recovery
followed by power loss before anyone looked.

The flag changes `sizeof(RecoveryRecord)`, so the existing length check rejects blobs
written by older firmware. That is the intended migration: those records are historical and
have by definition already been on screen.

### 10. FIXED — a reconnect loop that could not end, because the retry failed *because* the device was connected

Observed live on 2026-08-16 for over four hours. `src/wifi_setup.cpp`.

```
W wifi:sta is connected, disconnect before connecting to new ap
W wifi_cfg_net: Failed to connect to Pension LOVRIC, retry #/#
W wifi_cfg_net: Failed to connect to any network
I: WiFi connecting to Pension LOVRIC          <- ten seconds later, again
```

1. `wifi_cfg_is_connected()` reports false while the STA is genuinely associated
2. `reconnectWiFi()` gated only on that flag, so it called `wifi_cfg_connect()` every 10 s
3. `esp_wifi` **refuses** — "sta is connected, disconnect before connecting to new ap"
4. `wifi_cfg` records the refusal as a failed connection → "failed to connect to any network"
5. that failure keeps its state machine convinced it is disconnected → back to 1

Self-sustaining, and nothing in it can end on its own. Measured at **~97 disagreements per
second, sustained for four hours** (`wifiFlagDisagreements` in `/api/sender/`, which is the
counter to watch — it is the signature of the manager having desynchronised from the driver).

What it cost while running: the provisioning AP raised (~11 KB of heap and an open AP),
`/api/wifi/status` reporting an empty SSID and IP, HTTP dropping out for 30–60 s at a time,
TLS handshakes stretching from ~8 s to ~23 s, and Google Sheets uploads failing with
`SEND_ERR_CONNECTION_FAILED` — all while the link itself was fine and ping ran at 0% loss.

**Nothing was lost.** `network_is_usable()` consults the interface rather than the manager,
so every send kept being attempted, and the offline queue absorbed the ones that failed:
two full outage-and-recovery cycles, queue peaking at 8, `droppedOverflow` 0. That
defensive check is load-bearing, not belt-and-braces.

Fixed in three parts:

- `sta_link_is_up()` factors out the interface check, and `reconnectWiFi()` returns early
  when the STA is actually up — no request, so no refusal, so nothing to misread. This
  prevents the state being entered.
- once the manager has disagreed for `WIFI_MANAGER_RESYNC_AFTER_MS` (5 min), the
  association is dropped once, rate limited by `WIFI_MANAGER_RESYNC_COOLDOWN_MS`, forcing
  its state machine to re-derive from reality. This recovers a device already in it.
- the `WIFI_CFG_EVENT_DISCONNECTED` subscription is restored, rate limited so it cannot
  storm — it fires per failed *attempt*, not per outage, which is why it was commented out.

Verified on hardware: `wifiFlagDisagreements` frozen at 5845 across two hours, having been
climbing at 97/s before.

**The root cause was physical.** RSSI was −91 dBm when this was diagnosed and −44 dBm once
the path improved, at which point the reconnects — and everything downstream — stopped. The
firmware fixes make a marginal link survivable; they do not make it good. Check RSSI first.

### 11. `sprintf` into a tight buffer in MQTT

`src/targets/mqtt.cpp` builds `m_topic[90]` with unbounded `sprintf`. Worst case is 82 bytes,
so it is safe only because `mqttTopic` is capped at 31 in `jsonconfig.h`. Should be `snprintf`.

### 12. Log lines are not atomic — fix committed, effect unproven

ThorLog emits a message as one `printf()` per character into the single process-wide,
line-buffered `stdout` shared by every task, so another task can interleave between any two
characters. `src/serialhandler.cpp` now buffers a whole line and emits one `fwrite`, bracketed
by ThorLog's prefix/suffix callbacks under a recursive mutex.

**The fix is unverified.** The only genuine interleaving ever captured was in a boot log
(`mDNS responder started, hostn      7986 aIm: eHTTP server started`), and no post-fix boot
log was captured to compare. Steady-state interleaving measured **0 occurrences across 1,121
message lines** after the fix and **0 across 1,361** before it — there was nothing to fix in
steady state. See dead end #1 below for what the apparent corruption actually was.

### 16. FIXED — a rewritten queue journal lost its head, and readings could be resent

`rewriteJournal()` collapses the journal to one entry establishing the head plus the sparse
remainder. It wrote that head as an ordinary `QUEUE_JOURNAL_ACK`, and a trailing comment
asserted this was sufficient: "re-establish it on load by treating the single head entry as
authoritative. That is what markTerminated() + advanceHead() do when they replay it."

They do not. `markTerminated()` files the sequence in the sparse set and `advanceHead()` only
ever absorbs `m_headSequence + 1`, which is 1 on a fresh load. So a head of 100 replayed as a
lone sparse entry and the head stayed at 0, destroying the "every sequence at or below this is
delivered" fact. Every already-delivered reading then looked pending, the 64-entry sparse table
filled, and the overflow path warns "sequence N may be resent" — duplicate rows downstream.

The journal is the **only** record of the head. `QUEUE_STATE_PATH` is declared and removed on
factory reset but never written or read, so nothing else carries it.

**Fixed** by giving the head its own kind, `QUEUE_JOURNAL_HEAD`, which `loadJournal()` applies
authoritatively. `markTerminated()` also gained a fast path for the in-order case, which needs
no sparse slot at all, and now attempts compaction before declaring the table full.

Verified by transcribing the algorithm and running both versions against the same journals:

| journal | before | after |
|---|---|---|
| `HEAD(100), ACK(102), ACK(105)` | head **0** | head **100** |
| 200 in-order ACKs | head 200 | head 200 |
| head written as `ACK(100)` (legacy file) | head 0 | head 0 |

That last row is deliberate: a journal written by older firmware still replays as it always
did, and is corrected by the first rewrite under this code.

**This is probably NOT what was observed on the production device**, and saying so matters more
than claiming the fix. That device logged "replayed 72 journal entries, head at sequence 0u"
with eight warnings. A rewrite only happens once the journal passes 2048 bytes — 256 entries —
so at 72 entries it had almost certainly never been rewritten, and the head had never been
written at all. The arithmetic fits a different cause: an early sequence that never reached a
terminal outcome, leaving the head permanently stuck at 0 while 72 later terminations
overflowed the 64 slots. Whether those sequences were legitimately never acknowledged or their
outcomes were lost could not be determined, and the evidence is gone — the queue was erased by
a filesystem flash before it could be examined.

`loadJournal()` now logs where the gap is and whether the table is full, which distinguishes
the two on any recurrence: a lost head puts the gap at sequence 1, a stalled reading puts it at
that reading. `QUEUE_MAX_SPARSE_TERMINATED` was deliberately left at 64 — raising it would
reduce the resend window without addressing a permanently stuck head, and the reason for the
current value has not been disproven.

### 15. FIXED — upgraded to esp_wifi_config 0.2.2; events moved to `esp_event`

`src/idf_component.yml` now pins `tonci22/esp_wifi_config@2b5adb7` — upstream `c69c036`
(0.2.2) verbatim, plus the same single commit setting `WIFI_ALL_CHANNEL_SCAN` and
`WIFI_CONNECT_AP_BY_SIGNAL` on the station config, cherry-picked onto a second branch
(`tiltbridge-0.2.2`) exactly as this entry anticipated. Still a commit and not a branch:
tracking `main` is what let 0.2.0 break the build with no change on this side.

**What the upgrade fixes.**

- `wifi_cfg_disconnect()` no longer clears `config.auto_reconnect`. 0.2.2 sets a runtime
  `reconnect_suppressed` flag that `wifi_cfg_connect()` lifts, so `reconnectWiFi()`'s
  manager-resync path can no longer permanently disable auto-reconnect the first time it
  fires. That was a live bug and it is gone by construction — **established by reading both
  versions of the function, not on hardware.**
- The auto-reconnect retry is scheduled (`reconnect_pending`) rather than a `vTaskDelay()`
  executed inside the disconnect handler, and each retry iteration clears `CONNECTED_BIT`
  before waiting on it. Those are the mechanism behind ~60 s of stale `state` after any
  disconnect the manager did not initiate, which `wifi_link_check_ap()` provokes on every
  move. **Expected to improve; not measured.**

**What it does not fix**, as this entry predicted: access-point selection. `scan_method` and
`sort_method` are still absent from both connect sites in 0.2.2, which is why the fork
survives the upgrade rather than being retired.

**What the migration touched.**

- `wifi_setup.cpp` — seven `esp_bus_sub(...)` became
  `esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_*, ...)`, driven off a table so
  that a registration which fails is logged rather than discarded as `esp_bus_sub()`'s result
  was; the seven handlers take the esp_event signature; the `len < sizeof(...)` payload
  guards became NULL checks, the event id now fixing the type; the `wifi_cfg_config_t` is
  built from `WIFI_CFG_DEFAULTS` and overridden by assignment.
- `main.cpp` — `esp_bus.h` and `esp_bus_init()` dropped, with nothing replacing them:
  `initWiFi()` already creates the default event loop immediately before it registers.
- `sdkconfig.defaults` — `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096`; see the risk below.
- `esp_bus` has no remaining user and no longer appears in `dependencies.lock`.

**One claim in the previous version of this entry was wrong.** It said `WIFI_CFG_DEFAULTS`
would clear the ~20 `-Wmissing-field-initializers` warnings the initialiser emitted. It does
the opposite: GCC exempts designated initialisers from that warning in C but not in C++, and
the macro leaves more fields unnamed than the hand-written initialiser did, so 20 warnings
became 49. Suppressed with a scoped `#pragma GCC diagnostic` around the initialiser, which
is the honest description — every one of those fields is value-initialised on purpose.

**Running on the event loop task broke mDNS, and that is fixed.** The handlers no longer
have a task of their own; they run on the system event loop task, shared with `WIFI_EVENT`
and `IP_EVENT`. `on_wifi_got_ip()` called `mdnsReset()` directly, and `mdns_free()` /
`mdns_init()` unregister and re-register the mDNS component's own esp_event handlers — which
that loop cannot do inline. `esp_event_handler_unregister_with_internal()` attempts a
non-recursive `xSemaphoreTake(loop->mutex, 0)`; on the loop's own task the mutex is already
held, so it takes the deferred path, which only marks the node `unregistered` and queues a
cleanup event. `mdns_init()`'s re-register then finds that still-present node, logs `handler
already registered, overwriting`, updates only its `arg` — without clearing `unregistered` —
and the queued cleanup deletes the node outright. mDNS is left with no event handlers.

**Proven on hardware**: three `handler already registered, overwriting` warnings on the
first reconnect, none after the fix. Fixed with `mdnsRequestReset()` /
`mdnsServicePendingReset()` — the WiFi handlers ask, loopTask performs, where the mutex is
not held and removal happens inline. It also keeps `mdnsReset()`'s failure path (a one
second delay and a restart) off the loop that carries IDF's networking callbacks.
**Anything else that registers or unregisters an esp_event handler must not be called from
these handlers either.**

What still runs on the event loop task: the LCD redraws, and `on_provisioning_stopped()`'s
HTTP route registration — httpd handlers, not esp_event, so unaffected by the above. Neither
waits on the network. **Neither has been stack-profiled**; the 4096 bump restores the budget
esp_bus gave them rather than being a measured high-water mark.

**A trap worth recording**: adding `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` to
`sdkconfig.defaults` did nothing. Every `sdkconfig.<env>` here is checked in and already
carried that symbol at 2304, and an existing sdkconfig value beats a default — the
regenerated files still read 2304 and the bump was inert until the per-env files were
edited too. Check the generated sdkconfig, not the defaults file.

**Verified on hardware** — `esp32_headless`, ESP32-D0WD-V3, on `/dev/cu.usbserial-0001`:

- Boot: all seven subscriptions registered (nothing logged a failure), then `connecting` →
  `connected` (channel 11, RSSI −51) → `got IP` → `provisioning stopped` → HTTP server up →
  mDNS registered. The payload casts survive the migration — SSID, channel, RSSI and the
  disconnect reason code all print correctly off `event_data`.
- `/api/wifi/status` answers with `Transfer-Encoding: chunked`, so 0.2.2's streaming JSON
  writer is live and ordinary clients handle it.
- Reconnect cycle via `POST /api/wifi/disconnect`: `WiFi disconnected … reason 8` →
  auto-reconnect → `Reconnected to WiFi after 6 s and 1 disconnect event(s)` → `mDNS
  responder restarted`. Afterwards `managerConnected: true`, `desyncEpisodes: 0`, and
  `tiltbridge.local` still resolved to the device, which is the mDNS fix above being
  exercised end to end.
- All six environments compile and link. `wifi_setup.cpp`, `wifi_link.cpp`, `mdns_setup.cpp`
  and `main.cpp` build without warnings; the three `-Wmissing-field-initializers` left in
  `src/` are pre-existing, in `bridge_lcd_impl.cpp`'s I2C config, and unrelated.

**Not covered**: a real access-point outage (only a software-initiated disconnect was
tested), the SoftAP/provisioning path, and any target with an LCD.

**Incidental confirmation for issue 10**: `POST /api/wifi/connect` while the STA is already
associated still reproduces the stuck connect loop in 0.2.2 — `esp_wifi_connect()` returns
"sta is connected, disconnect before connecting to new ap", the manager counts that as a
failed attempt, exhausts its three retries and raises the provisioning AP, while the link
itself stays up and `/api/network/` correctly reports `associated: true` with
`managerConnected: false`. `reconnectWiFi()`'s `sta_link_is_up()` guard is what keeps the
firmware out of this; going through the component's own HTTP API bypasses that guard.

---

## Firmware — unexplained

### 13. A 30-minute gap after changing the Google Sheets interval

Observed once, in the sheet, after a 10 → 15 minute change:

```
15:55:53  seq 638
16:03:34  seq 63C   +7m41s
16:03:50  seq 640   +16s      <- two uploads seconds apart (cause found and fixed)
16:33:50  seq 644   +30m00s   <- unexplained
16:49:06  seq 648   +15m16s   correct from here on
```

Record ids contiguous at +4 throughout, so **no reading was lost** — the device simply did
not send for 30 minutes.

The double upload was the unconditional send-now, now fixed. The 30-minute gap is not
explained. It matches `backoffDelay()` exactly — `900 << 1` = 1800 = `SEND_BACKOFF_MAX_SECONDS`,
and note that at a 900 s base the cap is reached at a *single* step past threshold, so 6
failures and 60 failures both give exactly 30 minutes. But that needs
`consecutiveFailures >= 6` (`SEND_BACKOFF_AFTER_FAILURES` is 5), and there were no failed
uploads, no retried ids, and `consecutiveSendFailures` was 0.

If it recurs, the firmware prints the reason verbatim — capture serial and look for:

```
GSheets v2: upload failed (http N)...
google_sheets backing off to 1800s after N consecutive failures.
```

---

## Google Apps Script — `GoogleSheets/post_tilt.gs`

### 14. Upload latency is ~50 Sheets round trips per request

Measured by splitting each upload at the 302 across 52 requests:

| leg | what it is | min | max | avg |
|---|---|---|---|---|
| POST → 302 | Apps Script running the sheet code | 6 s | 22 s | 10.6 s |
| 302 → body | second TLS handshake + echo fetch | 1 s | 7 s | 2.3 s |

All variance is Google's leg; the device's own work is a steady 1–3 s. **Upload latency is not
a firmware problem.** At ~150–200 ms per Sheets service call, ~56 calls ≈ 9.5 s, which is what
is observed. The multiplier is that four Tilts write to four different sheets.

The four fixes originally listed here have been applied, along with three more. Kept as a
record of where the calls went, because the same mistakes are easy to reintroduce:

| fix | what it was | now |
|---|---|---|
| `setBackground(null)` on every appended row | 1–2 round trips per row, clearing a fill a fresh row never had | applied only when there is a fill to apply |
| three `getRange()` calls on the same row | — | one `Range`, reused |
| `sortAppendedSheets` on every request that appended | full read of column A per sheet, growing with the fermentation, to check an ordering that in steady state is never violated | `appendMeasurementRow()` sets `state.appendedOutOfOrder` from capture times it already has; the sort runs only when it is set |
| `getWineSheetState` doing 3–4 separate backward scans | one per fact about the same tail rows | one `readSheetTail()` over columns A–F |
| a `getProperty()` per script property | 6–10 round trips per request | one `getProperties()`, cached per execution, dropped when the lock is taken |
| `ensureSystemLogSheet` / `ensureMonitoringSheet` | headers, formats and column widths reapplied on **every call** — and `safeLogEvent()` calls it per event, so one `DATA_GAP` cost ~12 trips | formatted on creation, then gated on a `*-format-v1` property |
| `readProcessedIdSet` | read all 20 000 retained ids on every request | reads `PROCESSED_ID_LOOKBACK_ROWS` (2000); older ids cannot still be queued on the device |

**Do not change:** the chunked backward scans (500-row chunks with early exit) are well
designed, and batching all readings into one request is already right.

---

## Not bugs — do not re-investigate

### 1. `0xFF` blocks in serial captures are a host artifact, not the device

Captured serial logs contain runs of **exactly 64 bytes of `0xFF`** replacing a window of an
otherwise intact line. `0xFF` is the UART idle level; a transmitter cannot emit 64 idle bytes
mid-message, and the text resumes perfectly afterwards. It is the macOS CP210x/USB read path,
and it appears identically across different firmware builds (90 blocks in one run, 144 in
another).

This was mistaken for task interleaving for a full day, partly because the capture script
decoded with `errors="replace"` before writing and destroyed the evidence. **Always capture the
raw undecoded stream** when diagnosing serial corruption.

### 2. Attaching to serial with DTR/RTS driven low reboots the device

`s.dtr = False; s.rts = False` before `open()` toggles the CP2102 auto-reset circuit; the boot
log then shows `rst:0x1 (POWERON_RESET)`. Use the plain constructor, which leaves the lines
alone:

```python
s = serial.Serial("/dev/cu.usbserial-0001", 115200, timeout=0.5)
```

A reconnect loop that sets the lines explicitly reboots the device on every retry.

### 3. Google Sheets rows are no longer ~20 s later each cycle

Every send timer is one-shot and re-armed **after** its upload finishes, so its real period
was `interval + upload duration`. For Google Sheets the upload is ~14–20 s, almost all of it
Apps Script's own execution (issue 14), so a 10-minute push actually landed every ~10m20s and
walked a full hour around the clock in about 25 days. It showed up in the sheet's own
timestamps because column A is the capture time, taken as the batch is built.

`dataSendHandler::rearmGSheetsTimer()` (`sendData.cpp`) now keeps an absolute deadline and
hands the timer only what is left of it, so the cadence holds however slow a given upload was.
Two deliberate exceptions drop the anchor and re-grid on the next normal push: the 5-second
backlog drain, and `backoffDelay()` when a target is failing — both are meant to be off
cadence.

**This is Google Sheets only.** Every other target still slips by its own upload duration.
That is not a bug report; it is where to look if one of them ever needs the same treatment.

### 4. A burst of repeated log lines on serial reattach is not a device fault

Reopening the port produces thousands of duplicated lines in one second — more than 115200
baud can carry, so it cannot be live output. It only ever occurs on reopen. Steady-state serial
output is ~4 B/s.

### 5. macOS Sequoia blocks local network access per app

macOS 15 requires explicit permission per app to reach LAN devices, and denies it by reporting
the address as **unreachable** — Chrome shows `ERR_ADDRESS_UNREACHABLE` while `curl` from
Terminal works fine. **A Chrome auto-update re-triggers this**, and the new binary only takes
effect when Chrome is restarted, so it breaks with no user action.

Fix: System Settings → Privacy & Security → Local Network → enable the app, then fully quit and
reopen it. Incognito does not help — it is an OS permission, not a browser setting.

### 6. The device's mDNS name follows `mdnsID`

It is `lovric.local` on the test device, **not** `tiltbridge.local`. The IP is more reliable
than the name: mDNS is link-local multicast (TTL 1) and cannot cross a router, VPN or mobile
connection, and guest/isolated Wi-Fi commonly blocks multicast while leaving normal traffic
working.

Google Sheets uploads continuing while the web UI is unreachable is the expected signature of
the *client* having moved networks — the device uploads outbound over its own Wi-Fi and never
involves the laptop.

### 7. An open web UI tab re-saves settings

A settings page left open holds the values it loaded and writes them back on save, silently
reverting a change made elsewhere. Observed: an interval set to 600 via the API was overwritten
with 900 eleven minutes later by a browser tab. Before the send-now fix, every such write also
queued an upload.

---

## Verification discipline

Every bug in the "open" list above was found by reading code or soaking hardware. The ones
that were *introduced* during this work all passed a test first. What the tests missed:

**A single sample can pass by coincidence.** A fix that measured an interval from
`lastAttemptTime` was verified once and reported working. It was broken: the anchor returned a
0..59 seconds component, and the one test happened to have both timestamps inside the same
minute, so the arithmetic came out right. Any test involving a clock must deliberately cross
the boundary the code could be wrong about — a minute, an hour, midnight, a wrap.

**Check the metric can fail before trusting it.** `staleEvents: 0` and `lastRecovery: null`
were reported as evidence of health across ~25 hours. They were guaranteed regardless, because
the mechanism that sets them could never fire. A green indicator is only evidence if you have
seen it go red.

**Instrumentation lies too.** A monitoring script decoded serial with `errors="replace"` before
writing, which destroyed the byte pattern that identified the corruption as a host USB artifact
— costing a day of chasing a firmware bug that did not exist. Log raw, decode later.

**Preserved defaults need a reason.** `HTTP_MAX_OPEN_SOCKETS` was left at 7 when the worker
pool that justified it was deleted, "to preserve behaviour". Nothing justified 7 any more, and
it starved the outbound TLS client of sockets. When you delete the reason for a value, the
value is a new decision, not a preserved one.

**Say which parts are unverified.** Several fixes here are correct by inspection but were never
observed working. Marking that explicitly is what stopped them being re-derived later, and it is
why every entry above separates proven from suspected.

---

## Environment

Build and flash commands, serial capture recipes, the HTTP endpoint list and the recovery
watchdog acceptance test are in [DEVELOPMENT.md](DEVELOPMENT.md). The Google Sheets side —
deployment, the column layout, the one constant that must track the device's push interval,
and `node GoogleSheets/test/run_tests.js` — is in [APPS_SCRIPT.md](APPS_SCRIPT.md).

Two things worth repeating:

- `uploadfs` **destroys** the device configuration, including per-device Tilt names and
  calibration. A firmware-only `--target upload` preserves it. Back up with
  `GET /api/settings/json/` and `GET /api/devices/` first regardless.
- Never combine `--target upload` with `--target buildfs --target uploadfs`; PlatformIO
  reorders and dedupes them and the firmware write silently disappears.
