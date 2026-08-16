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

### 4. Large stack frames on the 8 KB `loopTask`

- `src/targets/legacy_fermentrack.cpp:27` — `char tilt_data[TILT_ALL_DATA_SIZE + 128]` is
  ~4 KB, then calls `http_request()` which adds ~700 B plus `esp_http_client_perform()`'s own
  frame. ~4.7 KB of 8 KB in one call chain. Use the `measureJson` + heap + verify pattern
  already proven at `src/targets/fermentrack_2.cpp:292-311`.
- `src/http_calibration.cpp:355` — `char victims[16][272]` is ~4.3 KB, but only on a
  user-triggered factory reset.

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

### 9. `sprintf` into a tight buffer in MQTT

`src/targets/mqtt.cpp` builds `m_topic[90]` with unbounded `sprintf`. Worst case is 82 bytes,
so it is safe only because `mqttTopic` is capped at 31 in `jsonconfig.h`. Should be `snprintf`.

### 10. Log lines are not atomic — fix committed, effect unproven

ThorLog emits a message as one `printf()` per character into the single process-wide,
line-buffered `stdout` shared by every task, so another task can interleave between any two
characters. `src/serialhandler.cpp` now buffers a whole line and emits one `fwrite`, bracketed
by ThorLog's prefix/suffix callbacks under a recursive mutex.

**The fix is unverified.** The only genuine interleaving ever captured was in a boot log
(`mDNS responder started, hostn      7986 aIm: eHTTP server started`), and no post-fix boot
log was captured to compare. Steady-state interleaving measured **0 occurrences across 1,121
message lines** after the fix and **0 across 1,361** before it — there was nothing to fix in
steady state. See dead end #1 below for what the apparent corruption actually was.

---

## Firmware — unexplained

### 11. A 30-minute gap after changing the Google Sheets interval

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

### 12. Upload latency is ~50 Sheets round trips per request

Measured by splitting each upload at the 302 across 52 requests:

| leg | what it is | min | max | avg |
|---|---|---|---|---|
| POST → 302 | Apps Script running the sheet code | 6 s | 22 s | 10.6 s |
| 302 → body | second TLS handshake + echo fetch | 1 s | 7 s | 2.3 s |

All variance is Google's leg; the device's own work is a steady 1–3 s. **Upload latency is not
a firmware problem.** At ~150–200 ms per Sheets service call, ~56 calls ≈ 9.5 s, which is what
is observed. The multiplier is that four Tilts write to four different sheets.

Four fixes, cheapest first:

1. `post_tilt.gs:1568-1570` — `setBackground(null)` runs on **every** appended row and is
   overwritten three lines later on the first row of a day. A fresh row at the end of the sheet
   has no background to clear. 4 wasted round trips per request.
2. `post_tilt.gs:1558, 1568, 1573` — three separate `getRange()` calls on the same row.
3. `post_tilt.gs:4488` (`sortAppendedSheets`) runs on every request that appended anything, and
   `sortWineSheetByCaptureTime` reads *all* of column A per sheet (`:4631-4639`). In steady
   state readings always arrive in order, so this checks a condition that is essentially never
   true — and the payload grows for the whole fermentation. Only sort when a row was actually
   written with a capture time earlier than the row above it.
4. `post_tilt.gs:1293-1416` (`getWineSheetState`) does 3–4 separate backward scans that all want
   facts about the tail of the same sheet. One shared read of the last ~500 rows replaces them.

Items 1–3 are ~12 of ~56 round trips — roughly 2 seconds.

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

### 3. A burst of repeated log lines on serial reattach is not a device fault

Reopening the port produces thousands of duplicated lines in one second — more than 115200
baud can carry, so it cannot be live output. It only ever occurs on reopen. Steady-state serial
output is ~4 B/s.

### 4. macOS Sequoia blocks local network access per app

macOS 15 requires explicit permission per app to reach LAN devices, and denies it by reporting
the address as **unreachable** — Chrome shows `ERR_ADDRESS_UNREACHABLE` while `curl` from
Terminal works fine. **A Chrome auto-update re-triggers this**, and the new binary only takes
effect when Chrome is restarted, so it breaks with no user action.

Fix: System Settings → Privacy & Security → Local Network → enable the app, then fully quit and
reopen it. Incognito does not help — it is an OS permission, not a browser setting.

### 5. The device's mDNS name follows `mdnsID`

It is `lovric.local` on the test device, **not** `tiltbridge.local`. The IP is more reliable
than the name: mDNS is link-local multicast (TTL 1) and cannot cross a router, VPN or mobile
connection, and guest/isolated Wi-Fi commonly blocks multicast while leaving normal traffic
working.

Google Sheets uploads continuing while the web UI is unreachable is the expected signature of
the *client* having moved networks — the device uploads outbound over its own Wi-Fi and never
involves the laptop.

### 6. An open web UI tab re-saves settings

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
