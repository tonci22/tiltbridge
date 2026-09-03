# Known issues and dead ends

Open problems found by reading the code and by soaking live four-Tilt devices
(ESP32-D0WD-V3, `esp32_headless`) across many runs — latterly including a ~24 hour
network-correlation study and several days on the production device, whose Google Sheets
System Log is a second, independent record when the firmware's own logs are silent.

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

### 17. FIXED — the captive portal answered DNS for the whole LAN

`esp_wifi_config`'s captive DNS bound `INADDR_ANY:53`, so while the provisioning SoftAP was
up the device also answered DNS on its **station** address — every name, any query type,
with the portal's own `192.168.4.1` and a 60 s TTL. A provisioned device raising the portal
was therefore a rogue resolver on the network it was a client of.

**Proven on hardware.** With the AP forced up alongside the station (`POST /api/wifi/ap/start`):

```
dig example.com @192.168.1.56  ->  192.168.4.1   (malformed reply)
dig google.com  @192.168.1.56  ->  192.168.4.1
```

It is not an exotic state: `provisioning_mode = WIFI_PROV_ON_FAILURE` raises the portal
whenever a known network cannot be rejoined while the station netif may still hold a lease.
The production device did it **ten times in one day**, in ~60 s windows, roughly hourly.

Fixed in the fork (`tonci22/esp_wifi_config@1610265`, pinned from `src/idf_component.yml`) by
binding to the SoftAP netif address instead. Verified both directions: the LAN query now
times out, while a client on the SoftAP still gets `192.168.4.1` for every name and the
portal page still serves.

**Not fixed, deliberately**: the replies are malformed. `dns_build_response()` copies the
query, forces `ancount = 1` and appends an A record whatever the question was, so a AAAA or
TXT query is answered with an A record, and an EDNS OPT record in the additional section is
overwritten while `arcount` still claims it. Harmless for a captive portal whose clients only
need redirecting.

**This was NOT the cause of the household internet dropouts** — see "Not bugs" #8.

### 18. FIXED — an HTTP 200 saying "error" was read as success, and readings were dropped

The Apps Script answers a batch it cannot process with **HTTP 200** and
`{"status":"error","code":"WEBAPP_BUSY","acceptedRecordIds":[]}` — it could not take the
spreadsheet lock within its ten-second budget. Its own comment states the contract: *"Nothing
was persisted, so no record id is acknowledged and the device resends this batch unchanged
with the same ids."*

`send_to_google_v2()` broke that contract. It never read `status` or `code`, only that
`acceptedRecordIds` was a well-formed array — and an empty array is still an array — so the
refusal landed in the success branch: `result = true`, `setTargetStatus(SEND_OK)`, and
`consecutiveFailures` reset to zero.

The recovery could not work, for the same reason. The branch set `snapshot_due` so
`take_queue_snapshot()` would persist the undelivered readings, but that function's first act
is `queuePersistenceNeeded()`, which returns `consecutiveFailures > 0` when the network looks
usable — and the `SEND_OK` three lines above had just zeroed it. The snapshot stored nothing,
and the warning it left promised a retry that could never happen.

**Observed on production, 2026-09-02 19:58.** The System Log's `WEBAPP_BUSY` entry names
record ids `8F35BD-*-00000DB1`..`DB4`; the spreadsheet has no 19:58 row; the rows either side
are sequence `0DAD` (19:48) and `0DB5` (20:08). Four ids minted, refused, dropped.

Fixed by `statusIsAcknowledgement()` (accepts `ok` and `partial`, treats everything else as a
refusal, and a reply with no `status` at all as acceptable so an older script still works), and
by routing every undelivered path through one shared persistence block rather than a
`snapshot_due` flag the success had already defeated. **Verified on hardware** against a
stand-in endpoint returning that exact body: the reading was kept, resent under the same id,
and delivered once the endpoint recovered.

### 19. FIXED — a drained queue came back full on every reboot

Termination was tracked as a head sequence plus a sparse table of at most
`QUEUE_MAX_SPARSE_TERMINATED` (64) out-of-order entries. The head only advances through a
**contiguous** run, so one never-acknowledged sequence pins it permanently — a record shed for
space, a reading the server refused, any gap. Past that, those 64 slots are the only record of
what was delivered, and `markTerminated()` discards the rest.

`loadJournal()` replays every acknowledgement back through the same 64 slots on boot, so a
queue that had genuinely drained came back full — and again on the next boot.

**Reproduced on production, 2026-09-03.** Immediately before a restart: `queued 0,
bytesUsed 0`. Immediately after: `Queue ready: 366 pending, segments 2u..7u, next sequence
3517u`, followed by

```
W: Queue: sparse termination table full; sequence 287u may be resent.
```

for sequences 287, 288, 304-309, 352, 353 — records captured 27 Aug, written to the sheet
1 Sep, resurrected ever since. Six days of re-sending them, refused every time as duplicates
(`savedRows: 0`).

**The expensive part is not the wasted uploads.** `pendingCount() > 0` puts
`send_to_google_v2()` on the drain path every pass, so the LIVE reading is never collected:
current readings wait behind a backlog the sheet already has. A plausible contributor to issue
20, though not established as its cause.

Fixed by `purgeTerminatedRecords()`: the segments are rewritten to physically drop delivered
records, then termination state collapses to a single head below the lowest survivor and the
sparse table is emptied. What remains on flash **is** the pending set, which survives a reboot
with no journal replay. Every failure path is non-destructive — a failed rewrite leaves the
original segment intact, costing a duplicate, never a loss.

`QUEUE_MAX_SPARSE_TERMINATED` is still 64. Issue 16 was right that raising it would not have
fixed this: the cap was never the defect, keeping the history in RAM was.

**Verified on hardware**: `Queue: purged 1u delivered record from flash; head now 1u, 0
pending.` then `Queue ready: 0 pending` after a restart. **Not verified**: the >64 overflow
itself after the fix — rebuilding it needs more than 64 acknowledged records against a pinned
head, and the bench board could only see one Tilt.

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

### 20. A 2h43m hole on 2026-09-02 with no trace anywhere

15:14:49 to 17:58:00, production, four Tilts. No rows in the sheet, nothing queued, and no
explanation.

What the record ids establish. `assignIdentity()` runs in `collectCurrentReadings()`
(`sendData.cpp:411`), so an id proves a reading was collected. Sequence `0D3D` at 15:14:49 and
`0D81` at 17:58:00 is `+68` — 17 batches of four — so **sixteen batches were collected during
the gap and every one vanished**, reaching neither Google nor the queue.

- The System Log shows **nothing arrived** in that window (the `DATA_GAP` entry at 17:58 reads
  `previousCapturedReading: 15:14:49`), and no backlog was ever delivered afterwards.
- The queue was empty at recovery, so the failure path either never ran or its appends were
  refused. Both were silent at the time: persistence logged only `if (stored > 0)`.
- Only `+4` per interval was consumed, not `+8`, so exactly one collection happened per
  interval — not a live send *and* a snapshot.
- No `WEBAPP_BUSY` in that window, so issue 18 does not explain it.

Issue 19 is a plausible contributor: a stuck backlog keeps the sender on the drain path so the
live reading is never collected. But the queue was *empty* at recovery, which that alone does
not explain.

**Deliberately left open.** Both silent paths now log — `GSheets v2: queue REFUSED n of m
undelivered readings` and `Queue snapshot: queue REFUSED n of m readings` — so a recurrence
names itself instead of vanishing. Do not theorise further without those lines.

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

## Web UI — `tiltbridge_web_ui`

Found by driving the built UI at a real 390 x 844 viewport, which is the first time it had been
looked at below `md`. Both behavioural bugs made whole pages unreachable from a phone; neither
is visible on a desktop, which is why they survived.

### 1. FIXED — the mobile menu never closed, so it covered the page it had just opened

`src/App.vue`. The small-screen sidebar is a full-screen `Dialog` overlay. Its links called the
router-link `navigate` and nothing else, and no route hook closed the panel, so after tapping
Configure or a cloud target the route changed underneath an overlay that stayed up. It read as
"nothing happens".

Now closed by a `router.afterEach`, which also covers the back button and the `CloudConfigView`
→ `FermentrackConfig` redirect. The link handlers close it too, because re-tapping the row you
are already on is a duplicate navigation that vue-router aborts, so `afterEach` never fires.

### 2. FIXED — a parent nav item was an inert `<span>`, so the cloud targets were unreachable

`src/App.vue`. The desktop sidebar rendered a group parent as a headlessui `Disclosure`; the
mobile branch rendered it as a plain `<span>` with its children always expanded beneath it —
and that `<span>` read an `isActive` that only exists inside a `router-link` slot, so the class
binding was `undefined` on every render.

Both branches are now the same route-driven accordion. Deliberately **not** `Disclosure` with
`defaultOpen`: that prop is read once, and on a cold load of `/target/gsheets/` the read happens
before vue-router has resolved the initial navigation, so `route.matched` is still empty and the
group comes up collapsed. Expansion falls back to "expanded if the current route is inside it",
with an explicit toggle recorded per section.

### 3. FIXED — the Configure tab dropdown did nothing

`src/components/sitewide/TabContainer.vue`. Two independent faults in the same six lines:

- `function routeChange(e) { this.$router.push(e.target.value) }` inside `<script setup>`. A
  module-scoped function invoked as an event handler has `this === undefined` under ESM strict
  mode, so every pick threw `TypeError` before it could navigate. This is what made
  Configure → Offline Queue inert.
- The options were `<router-link custom>` wrappers rendering `<option>`. An `<option>` is not
  clickable, so the link half was never going to fire either; only the `:value` it computed did
  anything, and only because the change handler read it.

Replaced by `MobileRouteSelect.vue` — a plain `<select>` with a `useRouter()` push, its selected
option derived from `route.matched` so it still tracks the route when the sidebar or the desktop
tabs navigate instead. `TablessContainer` takes the same component, which is how the eleven
cloud targets became reachable on a phone without going back out to the menu.

### 4. FIXED — layout that only fails below `sm`

None of these break anything; all of them were wrong on a phone.

- The Tilts table forced the page sideways, and the temperature column — half the reason for
  the page — fell off the right edge. The table is now `max-w-full` inside an `overflow-x-auto`
  wrapper, so it shrinks to the viewport first and only scrolls the table if a row still cannot
  fit. Measured: 423 px of content in a 374 px box before, 374 px after.
- `AddCalibrationPointModal` was a hard-coded `w-96` (384 px) offset with `top-20`: wider than
  an iPhone 13 mini viewport, and pushed off the bottom in landscape. Both calibration modals
  are now centred in a scrollable flex wrapper with a `max-w-*` box.
- Key/value tables in About and the queue status panel were `px-6` per cell, which needed a
  horizontal scroll to read a two-column table. Now `px-3 sm:px-6`.
- `CheckboxField` put the description inline after the label, which reads as one run-on
  sentence the moment they share a wrapped line. Now stacked.
- The mobile top bar was a bare hamburger. It now names the current page, which is the only
  place that information appears on the pages that have no `<h1>` (About).

### 5. FIXED — a phone in landscape got the desktop sidebar, which cost it a third of its width

`src/App.vue`, `src/components/TiltList.vue`. The permanent sidebar appeared at `md` (768 px).
Every current handset in landscape is wider than that — 844 px on a 13/14, 932 px on a Pro Max —
so turning the phone sideways swapped the hamburger for a fixed 256 px sidebar and left 540 px
of content. The Tilts table needs 797 px for its landscape column set, so **gravity and
temperature — the entire point of the page — sat off the right-hand edge** behind a scroll.

The sidebar now appears at `lg` (1024 px), so 768–1023 px keeps the hamburger and the full
width. Measured on the flashed test board: 844 x 390 gives a 796 px content box for a 797 px
table, no scroll, and 932 px fits with room to spare.

RSSI moved from `lg` to `sm` in the same pass, which is what makes it a landscape column: the
smallest phone in landscape is 667 px, so `sm` (640 px) is the breakpoint that means "turned
sideways". Portrait is untouched — 390 px still shows name, gravity and temperature only.
Colour deliberately stayed at `md`: the row already carries its colour as the stripe down the
left edge, so RSSI is worth more than a second copy of it.

**Still scrolls at 1280 px.** Nine columns need 1188 px and the sidebar leaves 976, so the
desktop table scrolls inside its own box. That is an improvement — before the wrapper existed
it pushed the whole page sideways — but if it becomes annoying, moving `MAC` and `Model` to
`2xl` fits the rest into a 1280 px laptop.

**Verified** at 390 x 844 via CDP `Emulation.setDeviceMetricsOverride` — note that
`--window-size` alone cannot go below ~500 px, so a `--screenshot` at `--window-size=390,844`
is a 390 px crop of a 485 px layout and will invent overflow that is not there. Widths
checked: 390, 667, 844, 932, 1280. Desktop re-checked at 1280 px: sidebar accordion, desktop
tabs, and both mobile-only elements hidden. All of the above was then re-driven against the
flashed `lovric-test` board rather than a mock.


### 6. FIXED — the About tables needed a horizontal drag, and a mock hid it

`about/UptimeStatsPanel.vue`, `about/WifiLinkPanel.vue`, `about/SenderHealthPanel.vue`,
`config/TiltBridge/QueueStatusPanel.vue`. Reported from a phone: the values ran off the right
edge and had to be dragged into view.

Two causes, both in the shared key/value table markup:

- The value cells were `whitespace-nowrap`. On the real device the firmware version renders as
  `TiltBridge v2.0.0-beta5[phase1-reliability-and-offline-queue] (699953c)` — about 71
  characters — and uptime, reset reason, heap and the RSSI range line are all long too. Now
  `md:whitespace-nowrap`, so they wrap below 768 px and stay on one line above it.
- The wrapper was `py-2 align-middle inline-block min-w-full`. `inline-block` is shrink-to-fit,
  so the box grew to the table's max-content width (619 px against a 390 px phone) and the
  table was never asked to shrink — the `overflow-x-auto` ancestor scrolled instead. Dropping
  `inline-block` makes the box the viewport width, which is what lets the wrapping take effect.

Measured on the flashed board: all three About tables and the queue status table are 390 px in a
390 px scroll box, `mustScroll: false`, at 390/844/1280. Desktop is visually unchanged except
that the tables now fill the card instead of ending short of its right edge.

**This is the entry to read before trusting a mock.** The layout pass that produced entries 1-5
verified About against a stub whose version, uptime, reset and WiFi fields were empty or
`Loading...`, so the longest strings on the page never rendered and the overflow was invisible.
The Tilts table was caught only because the mock happened to carry a long `friendlyName`. Point
the dev proxy at a real device — `vite --config` with a one-file override is enough — or give
the mock deliberately worst-case strings.

### 7. FIXED — a flashed UI came up blank in an already-open browser, because chunk names carried no hash

`tiltbridge_web_ui/vite.config.js`, `src/idf_static_files.cpp`. Reported as "why is the help
page empty" immediately after an `uploadfs`. The whole chain was observed:

- `vite.config.js` set `entryFileNames`/`chunkFileNames` to `[name].js` — **no content hash**,
  so every build wrote `index.js`, `HelpPage.js`, … over the same URLs.
- `idf_static_files.cpp` served everything outside `conf/` with `max-age=600`. Its comment said
  "The UI assets are still cached: they only change when the filesystem is reflashed" — which is
  the assumption that fails, because a reflash is exactly when they change.
- So for ten minutes after a flash a browser could hold OLD chunks under the NEW build's names.
  The old `HelpPage.js` began `import{_ as r}from"./_plugin-vue_export-helper.js"`; that build
  inlined the helper away, so the device returned **404** for it. The dynamic `import()` behind
  the lazy route rejected and `router-view` rendered nothing — blank, with no visible error.
- Tilts was unaffected because `router/index.js` imports `TiltList.vue` statically, so it lives
  inside `index.js`. Every other route is lazy.

**The fix is two halves that have to agree with each other.** Hashing alone is not enough: a
cached `index.html` still names files the flash deleted.

1. Assets are content-hashed and moved under `assets/` (`assets/[name]-[hash].js`).
2. Three cache tiers in `idf_static_files.cpp`, keyed off that path:
   `conf/` and **any HTML** → `no-store`; `assets/` → `max-age=31536000, immutable`;
   everything else (root favicons, `wifiui/`) → the old short `max-age`.

`no-store` on HTML is the half that matters, because every SPA route hands back `index.html`.
It is now always fetched from the device, so it always names assets that exist.

**Verified on the flashed board**, headers and behaviour: `/`, `/index.html`, `/help/`,
`/about/`, `/calibrate/Red/` all `no-store`; hashed assets `immutable`; `/favicon.ico` and
`/site.webmanifest` `max-age=600`; `/conf/…` `no-store`; a missing `/assets/nope-1234.js` still
404s rather than silently returning `index.html`. On a second page load Chrome refetched the
navigation (990 bytes, `transferSize` non-zero) and served all 141 KB of hashed assets from
cache (`transferSize: 0`) — so this is also *less* traffic than before, where the full 141 KB
was re-downloaded every ten minutes with no revalidation.

### 8. mklittlefs silently drops files it cannot add, and PlatformIO still reports SUCCESS

Found by the fix for 7, which walked straight into it. **This is the entry to read first when
the UI misbehaves after a flash.**

`mklittlefs` enforces LittleFS's `LFS_NAME_MAX` of **32 characters** on a filename. Hashing the
chunk names pushed one over: `SendTargetErrorMsg-DYduZCLE.js.gz` is 33. mklittlefs printed

```
unable to open '/assets/SendTargetErrorMsg-DYduZCLE.js.gz.
error adding file!
Error for adding content from assets!
```

then **abandoned the rest of that directory** — and `pio` still ended in `[SUCCESS]`. Four of
the twenty-four assets never reached the image, `esptool`'s "Hash of data verified" only proved
the *incomplete* image had been written faithfully, and every cloud target page went blank:
`Brewfather-*.js` statically imports `SendTargetErrorMsg-*.js`, and a 404 on a static import
fails the whole dynamic import, so the error names the chunk you asked for rather than the one
that is missing.

Confirmed by `grep`-ing the generated `littlefs.bin` for each filename: 20 present, 4 absent.
32 characters is the exact boundary — `PushIntervalField-CrB3-cmf.js.gz` (32) survived,
the 33 did not.

Three things now stand in the way of it recurring:

- `fsSafeName()` in `vite.config.js` truncates the stem to whatever is left after the hash and
  both extensions (`.gz` included, since vite-plugin-compression appends it), so a UI chunk
  name cannot exceed 32 whatever a component is called.
- `_check_name_lengths()` in `tools/build_ui.py` fails the build before mklittlefs runs, and
  covers `public/` files, which bypass the naming above.
- `verify_fs_image()` is a post-action on the image node: it checks every file in `data/`
  appears in the built `littlefs.bin` and exits non-zero otherwise. This is the backstop for
  *any* silent mklittlefs failure, not just long names — a full filesystem would behave the
  same way. A good build now prints
  `Filesystem image verified: all 43 files from data/ are present.`

Both guards were negative-tested (a 33-character name and a file withheld from a synthetic
image each exit 1), not just observed passing.

### 9. FIXED — /calibrate/<id>/ returned 404, so the page could not be opened or refreshed by URL

`src/idf_static_files.cpp`. `idf_static_register_spa_routes()` held a hardcoded list of Vue
paths, and the parameterised calibration route was not in it — there is no fixed path to match.
The `/*` catch-all then tried to open `calibrate/Red/` as a file and returned 404. Clicking
through from the Tilts page worked, because that navigation never leaves the SPA; a refresh or a
bookmark did not.

Fixed by registering `/calibrate/*`. Order matters and holds: `http_server.cpp` calls
`register_spa_routes()` before `register_catchall()`, and `esp_http_server` matches handlers in
registration order. Verified on the board: `/calibrate/Red/` and
`/calibrate/C7:A5:3F:7A:33:13/` both return `index.html` and render "Calibrating Red Tilt".

**The list is still hand-maintained**, and `router/index.js` carries a comment saying these
paths must be kept in sync with the firmware. A route added to the Vue router and not here will
work when clicked and 404 on refresh. Serving `index.html` for any extension-less path would
remove the class of bug, and was left alone as a wider change than this fix needed.

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

### 8. TiltBridge is not causing the household internet dropouts

Investigated at length on 2026-08-27/29 after the ESP32 was suspected. **It is not the cause**,
and this was measured, not argued.

A monitor sampled router reachability, WAN reachability (`1.1.1.1`) and DNS-through-the-router
every 30 s for ~24 hours alongside the device's own state (`netwatch.log`). Across **2,768
samples**:

| | |
|---|---|
| `net=FAIL` (WAN) | 87 samples (3.1%) |
| `router=FAIL` | **0** |
| `dns=FAIL` | **0** |
| `net=FAIL` while the rogue AP was up | **0** |

Every failure was the WAN leg alone, with the LAN and the router's resolver working throughout.
The AP was up for ~1.5% of samples; if outages and the AP were independent you would still
expect an overlap or two, and there were none.

Decisive single event: a **25-minute** flapping WAN outage on 2026-08-29 04:18-04:44, during
which the device sat at −49 dBm with `ap=False` and its counters frozen at `out=57 roams=66
desync=47` — completely idle while the internet fell over. Earlier, a 3-minute outage at
23:55-23:58 with the same signature.

Mechanisms checked and excluded:

- **Rogue-AP auto-join** — needs `TiltBridgeAP` saved on the affected device; it is not, and it
  would affect one client, not all of them.
- **The DNS hijack of issue 17** — real, but the device ignores broadcast queries (tested: five
  other LAN hosts answered `192.168.1.255:53`, the ESP32 did not) and LAN clients are handed
  `192.168.1.1` as their resolver, so nothing asks it.

**Where to look instead**: the router advertises `domain_name_server: {192.168.1.1, 0.0.0.0}`.
That invalid secondary is a plausible cause of intermittent "connected, nothing loads" on
clients that fail over to it — though `dns=FAIL: 0` across the whole run does not support it
either. The WAN-only pattern points upstream, at the ISP.

### 9. A non-empty queue while the spreadsheet is complete is not duplication, and not data loss

Reported on 2026-09-03: `lovric.local` showed **8 queued readings** while every Tilt's sheet
already held every reading, `uploadStatus` sat on `RETRYING`, "Send backlog now" appeared to do
nothing, and flushing eventually added one row per sheet only ~35 s after the previous one
(16:23:59 then 16:24:34). All of that is the design working, plus one 60-second WiFi desync.

**Why the queue filled at all.** A send failed *from the device's point of view* while Google
had in fact processed it — `wifiDesyncEpisodes: 1, wifiDesyncLongestSec: 60` at the time.
`docs/phase1/06-persistent-queue.md` names this case explicitly: "'Failed' includes a POST the
server actually processed whose response was lost, and re-sending the identical `recordId` is
what lets the script's duplicate suppression absorb that." The first failure persists **those
exact records** rather than capturing fresh ones, precisely so the ids match on the retry.

**Why 8 and not 4.** Four Tilts: the 4 originally-failed records plus one snapshot round of 4.
Once a backlog exists the live path stands down and `take_queue_snapshot()` captures on
`queueSnapshotIntervalSec` — `sendData.cpp:559` (`queuePersistenceNeeded()`) writes nothing
until `pendingCount() > 0` or the network is unusable. `oldestReadingAgeSec` was 514 s against a
600 s interval, i.e. exactly that one round.

**Why the flush added an off-cadence row instead of 8 duplicates.** The two kinds of record
behave differently, and both behaviours are correct:

| record | on re-send |
|---|---|
| the originally-failed 4 | same `recordId`s the sheet already has, so `_processed_ids` suppresses them — no new rows. `post_tilt.gs:838`: the device deletes a record only when its id comes back in `acceptedRecordIds`; delivery is at-least-once with dedup in the script |
| the snapshot 4 | `collectCurrentReadings()` took **fresh** values on the snapshot timer's own phase, not the `:xx:59` push cadence — new captures, new ids, so they legitimately add one row per Tilt at an odd timestamp |

**Why it looked stuck.** `RETRYING` is honest: the POST → 302 leg of an Apps Script call alone
is 6–22 s (entry 14 above), four Tilts write four different sheets, and the sender held the lock
mid-request. It drained on its own about 20 minutes after the desync and returned to
`queued=0, bytesUsed=0, IDLE, consecutiveSendFailures=0, droppedOverflow=0`.

**"Send backlog now" doing nothing was real, and is fixed.** `http_server.cpp` only sets
`data_sender.send_backlog_now`; `send_to_google_v2()` takes the sender lock *before* clearing it
and returns early without clearing when the lock is held, so the request is never lost — but
nothing on the page said so, and the store used to optimistically claim `uploadStatus =
"SENDING"`, which the next poll replaced with `RETRYING`. `/api/queue/` now reports
`backlogRequested`, and the panel disables the button and says the upload is queued until the
firmware picks it up.

**The residual wart, deliberately left.** A lost response is indistinguishable from a lost
request, so the device treats a succeeded-but-unacknowledged send as an outage and samples into
the queue. Those samples are genuine new readings, so flushing them adds slightly off-cadence
rows carrying information the sheet already had. Closing that would need the script to be able
to answer "did you already take id X" before the device decides to persist, which is another
round trip on every failure. Not worth it: the cost today is one extra row per sheet per blip.

**Do not** treat a non-empty queue with a complete spreadsheet as duplication, and do not clear
the queue to "fix" it — the readings in it are either already-acknowledged ids that will be
suppressed, or real captures that belong in the sheet.

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
