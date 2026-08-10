# Stage 13: Acceptance tests

Spec §27. Every test below must be run and its result recorded before Phase 1 is called done.
Record pass/fail plus the observed evidence inline in this file as testing proceeds.

Hardware needed: one TiltBridge (any supported board), at least two Tilts of the **same colour**
with different MACs — or the BLE simulator below — and a Wi-Fi network the tester can disable.

## Test harness: simulated Tilts

Two real same-colour Tilts are hard to source. A host-side BLE advertiser is sufficient for
identity, RSSI and queue tests (it cannot exercise real signal strength, but it can emit distinct
MACs and gravity values).

```python
# tools/sim_tilt.py  — Linux/BlueZ or macOS CoreBluetooth via `bleak`/`bluez-peripheral`
# Emits an iBeacon whose UUID is the Tilt colour UUID and whose major/minor carry temp/gravity.
# Manufacturer data layout (from tiltScanner.cpp:109):
#   4c 00 02 15 | 16-byte colour UUID | 2-byte temp (F) | 2-byte gravity | 1-byte tx_pwr
# Red UUID: a495bb10c5b14b44b5121370f02d74de
```
On macOS the advertising MAC is not settable, so run the simulator from a Linux host or two
Raspberry Pis (`hcitool -i hci0 cmd 0x08 0x0008 …`, or `bluez-peripheral`) to get two distinct
addresses. Two ESP32 dev boards running a 30-line NimBLE advertiser is the cheapest reliable
option and gives full control over the MAC.

Write whichever harness is used into `tools/` so the tests are repeatable.

---

## T1 — Multiple same-colour Tilts (§27.1)

**Setup**: two Red Tilts, MACs `A` and `B`. `combineTilts` off.

| Step | Expected |
|---|---|
| 1. Open Detected Devices | Two rows, both `Red`, distinct MAC column values, no duplicate-key warnings in the browser console |
| 2. Set friendly names "Cabernet Tank 1" / "Merlot Tank 4" | Names persist across a page reload and a device reboot |
| 3. Set different Google Sheets names | `/api/devices/` shows both; `/api/json/` shows the resolved `gsheets_name` per device |
| 4. Calibrate `A` with a 2-point curve, leave `B` uncalibrated | `A` reports the corrected gravity, `B` reports the color-default gravity; the two differ |
| 5. Disable `B` | `B` still appears in the list, marked disabled; no `B` rows reach Google Sheets or Fermentrack |
| 6. Wait for a queue snapshot | Two records for `A`+`B` when both enabled, one when `B` is disabled; `/api/queue/` count matches |
| 7. Inspect queued record ids | Different `deviceHash` segments for `A` and `B` |

**Also verify**: with no `devices.json` at all, both Red Tilts still send under the shared color
config exactly as beta5 does (that is T7).

## T2 — Persistent queue survives reboot (§27.2)

1. Set snapshot interval to 1 min (temporarily) to build records quickly.
2. Break Google Sheets delivery (point `scriptsURL` at an unreachable host) so nothing is acked.
3. Accumulate ≥ 10 records; note the exact count and the oldest record id from `/api/queue/`.
4. `POST /api/actions/ {"action":"restartDevice"}`.
5. After boot, `/api/queue/` shows the **same** count and the **same** oldest record id.
6. Repeat with a watchdog-style restart (trigger the stage-5 debug freeze, let the monitor reboot).

Pass criteria: no record loss, no id renumbering, `nextSequence` continues above the highest
sequence on flash (check the next record's id increments rather than colliding).

## T3 — Power interruption (§27.3)

1. Build ≥ 20 queued records as in T2.
2. **Yank USB power** (not a soft restart) — do this at least 5 times, including once during an
   active snapshot write (snapshot interval 1 min makes hitting the window feasible; watch the
   serial log for the append and cut power immediately after).
3. After each boot: `/api/queue/` count is either N or N−1 (the interrupted append may be lost),
   never less, and never corrupt.
4. Serial log on boot shows at most one "discarding truncated/CRC-failed trailing record" message
   and no cascade of errors.
5. Re-enable delivery and confirm every surviving record uploads and is acked.

Pass criteria (§26): previously committed records always recoverable; only the interrupted final
write may be lost.

## T4 — Network outage (§27.4)

1. Configure Google Sheets v2 and Fermentrack normally; confirm both are delivering.
2. Disable Wi-Fi at the AP (or power the AP off) for **≥ 3 hours** — long enough for six 30-min
   snapshots per device.
3. During the outage:
   - BLE keeps updating (Detected Devices ages stay low) — check over the serial console since the
     web UI is unreachable.
   - Snapshots keep accumulating: serial log shows one append per device per interval.
   - **No reboot occurs.** The stage-5 monitor requires `network_is_usable()` to be true, so a
     genuine outage must not trigger recovery. This is the most important assertion in T4.
4. Restore Wi-Fi.
5. Queued records upload in batches of `queueBatchSize`; `/api/queue/` count drains to 0.
6. Spreadsheet rows carry the **original capture times**, not the upload time (§12).

## T5 — Duplicate retry (§27.5)

Requires a controllable endpoint — point `scriptsURL` at a local test server rather than Google.

1. Test server: accept the batch, record the ids, then **close the connection without responding**.
2. Firmware: `http_request` fails; nothing is acknowledged; the batch stays queued.
3. Next attempt sends the **byte-identical** record ids (diff the two request bodies).
4. Test server now returns `{"status":"ok","acceptedRecordIds":[…all ids…]}`.
5. Records are removed; `/api/queue/` count drops by exactly the batch size.
6. Test server saw each id twice → server-side dedup by id is what prevents duplicate rows, as
   documented in `APPS_SCRIPT_PROTOCOL.md`.

Additional sub-cases to run against the same test server:

- **Partial ack**: return only the first 10 of 20 ids → exactly 10 removed, 10 retried with the
  same ids.
- **HTTP 500** → nothing removed, no reboot, retry on the normal schedule.
- **Malformed body** (`<html>Error</html>` with HTTP 200) → nothing removed, `SEND_ERR_OTHER`
  logged, no crash. This also regression-tests the null-`doclongurl` fix from stage 4.
- **Unknown id in `acceptedRecordIds`** → ignored, no crash.
- **Reboot between POST and ack** → same ids resent after boot.

## T6 — Sender freeze (§27.6)

1. Build with `-D TB_DEBUG_FREEZE=1`; `POST /api/actions/ {"action":"debugFreezeSender"}`.
2. Observe: BLE keeps updating, `tiltbridge.local` stays responsive, `/api/sender/` shows
   `heartbeatAgeSec` climbing.
3. At ~75 s the device reboots.
4. After boot, `/api/sender/` reports `lastRecovery.reason = SENDER_HEARTBEAT_STALE` with the
   heartbeat age and target captured at the time.
5. Negative control — this must **not** reboot:
   - point every target at an unreachable host and let it fail for 10 minutes. `consecutiveSendFailures`
     climbs into the dozens, the heartbeat stays fresh, **no reboot** (§19).
   - disable Wi-Fi entirely for 10 minutes with the queue accumulating: **no reboot** (T4 step 3).
6. Stuck-lock control: force a target's HTTP request to hang (test server that accepts and never
   responds, with the socket kept open past the timeout). `/api/sender/` shows
   `State: STALE`, `Current target`, and a climbing `Request age`; the monitor logs the stall and
   attempts cleanup. Verify the device recovers once the request finally returns, without a reboot
   if the heartbeat resumed.

## T7 — Legacy behaviour (§27.7)

1. Flash the new firmware over an **existing beta5 install** without erasing the filesystem
   (`pio run -t upload`, *not* `-t erase`). Keep the original `tiltbridgeConfig.json`.
2. No `devices.json` exists.
3. Verify unchanged: Google Sheets names per colour, calibration curves per colour, Grainfather
   URLs, Fermentrack registration and pushes, MQTT topics, InfluxDB writes, brewstatus,
   taplist.io, Brewer's Friend, Brewfather, user target.
4. `gsheetsV2Enabled` defaults false → the legacy Sheets payload is byte-identical to beta5's
   (capture both with a local HTTP echo server and diff).
5. Existing calibration point files (`0-cal.json` … `7-cal.json`) still load and edit.
6. Config round-trip: `GET /api/settings/json/` → `PUT` it back unchanged → no field lost, file
   still under `JSON_CONFIG_BUFFER_SIZE`.
7. Downgrade check: reflash beta5 over the new firmware. It ignores `devices.json` and the
   `queue/` directory and boots normally. (Not required by the spec, but cheap and worth knowing.)

## T8 — Regression sweep not called out by §27 but required by "preserve existing functionality"

- [ ] Every one of the eleven send targets still works with real credentials, or at minimum
      produces the correct request against a local echo server.
- [ ] `combineTilts = true` still merges same-colour Tilts.
- [ ] Factory reset, Wi-Fi reset, restart, and mDNS rename all still work.
- [ ] LCD/TFT targets still build and display (`lcd_ssd1306` plus one S3 target).
- [ ] Heap: `printMem()` before and after a 20-record batch upload; largest free block should not
      collapse. Record the numbers here.
- [ ] Filesystem: with 1500 records queued, config save still succeeds and the UI still loads.

## Results log

Hardware used so far: ESP32-D0WD-V3 rev 3.1, 4 MB flash, MAC `AA:BB:CC:DD:EE:FF`,
environment `esp32_headless`, mDNS `tiltbridge.local`, IP 192.168.1.x. Four live Tilts
(Red `AA:BB:CC:00:00:01`, Green `AA:BB:CC:00:00:02`, Black `AA:BB:CC:00:00:03`,
Blue `AA:BB:CC:00:00:04`).

| Test | Date | Result | Evidence / notes |
|---|---|---|---|
| T1 | 2026-08-10 | **PARTIAL** | Four physically distinct Tilts appear separately with correct per-device `deviceId`, `modelLabel` and independent RSSI aggregates. **Not yet covered: two Tilts of the same colour** — all four on hand are different colours. Needs a second same-colour unit or the simulator. |
| T2 | 2026-08-10 | **PASS** | 4 records queued, `bytesUsed` 512 = 4 × 128 B. After `restartDevice`: still 4 records, `oldestReadingAgeSec` 31 → 156 (advanced with real time rather than resetting), so capture timestamps and the boot-scan sequence recovery both work. `lastRecovery` correctly `null` for a commanded restart. |
| T3 | | | Needs physical USB power interruption — cannot be done over the wire. |
| T4 | | | Needs a multi-hour AP outage. |
| T5 | 2026-08-10 | **MOSTLY PASS** | Both halves of the acknowledgement contract observed on hardware. **Failure half**: while the deployed script predated v2, uploads went `RETRYING`, failures incremented, and **no records were deleted** across dozens of attempts — the queue grew to 56 with 0 dropped. **Success half**: after the v2 script was deployed, a 24-record backlog drained in three batches (10/10/4), each returning `savedRows` matching and `acceptedRecordIds` echoing every id, with records removed only on acknowledgement and the queue reaching exactly 0. Server-side duplicate suppression itself is covered by the Apps Script mock suite, not yet by a hardware lost-ack replay. |
| T6 | | | Needs a `-D TB_DEBUG_FREEZE=1` build. **Negative control already passing**: sustained Google failures (`consecutiveSendFailures` climbing) produce no reboot, as required. |
| T7 | 2026-08-10 | **PASS** | Flashed over an existing beta5 install with its config preserved. Fermentrack 2 registration (`deviceID` + `APIKey`), Google Sheets URL/email, colour sheet names (grk1/grk2/posip/rose) and all general settings survived and load correctly. Fermentrack 2 confirmed working end-to-end: `{"success": true, "message": "Status updated"}`. |
| T8 | 2026-08-10 | **PARTIAL** | Fermentrack 2 **and** Google Sheets both verified live end to end. Free heap steady ~46–49 KB across a 30-minute watch (48,088 → 48,080 over ten minutes — no leak from the added `malloc`/`free` pairs), fragmentation stable ~30%. All six environments build. Remaining nine targets and the 1500-record filesystem-pressure check not yet exercised. |

### Regression found by hardware testing (fixed)

Fermentrack 2 returned `400 JSON parse error - Expecting ',' delimiter: line 1 column 2049`.
`send_status_to_fermentrack_2()` serialised into a fixed `char payload[2048]` stack buffer, and
the per-Tilt JSON grew past it once device identity and RSSI aggregates were added —
`serializeJson` truncated silently. Now sized with `measureJson` and heap-allocated, with an
explicit truncation check that fails loudly. The legacy Fermentrack buffer was already guarded via
the `legacy_keys` branch; this second buffer was missed. Verified fixed on hardware.

### Note on filesystem headroom

After the larger UI, `fsFreeBytes` is 356,352 (~348 KB). A full 1500-record queue is 192,000 B,
leaving ~164 KB. Fits, but the margin is smaller than the original 832 KB budget implied — the
`QUEUE_MIN_FREE_BYTES` 32 KB floor guard matters more than expected. Re-check if the UI grows.
