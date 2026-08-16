# Phase 1 implementation status

> **This is a historical implementation log, not a description of the current code.**
>
> It was accurate when Phase 1 landed and has since drifted. Where it disagrees with the
> code, the code wins. For what is true now:
>
> | For | Read |
> |---|---|
> | Open bugs, and theories already disproven | [../KNOWN_ISSUES.md](../KNOWN_ISSUES.md) |
> | Build, flash, serial, acceptance tests | [../DEVELOPMENT.md](../DEVELOPMENT.md) |
> | The Google Sheets script and its layout | [../APPS_SCRIPT.md](../APPS_SCRIPT.md) |
>
> **Corrections to claims made below**, in order of how much trouble they could cause:
>
> 1. **The recovery watchdog described under "The field-failure fix" was inert until
>    2026-08-15.** Its boot-grace guard compared `uptimeSeconds()` — which returns the 0..59
>    seconds *component*, not total uptime — against `RECOVERY_GRACE_SEC` (180), so it
>    returned early on every call and could never fire. Phase 1's central safety mechanism
>    therefore did not work for the whole period this document calls it done. Fixed and
>    verified firing on hardware; see KNOWN_ISSUES.md #8.
> 2. **Stage 13 is no longer "not started".** The recovery-reboot acceptance test (T6) has
>    been run on hardware — the recipe is DEVELOPMENT.md §8.1.
> 3. **The wine sheet layout below is two revisions out of date.** It describes 19 columns at
>    `wine-layout-v13-v2-diagnostics`; the layout is now 12 columns at
>    `wine-layout-v16-quality-as-fill`, and the "Average quality" column no longer exists.
>    See APPS_SCRIPT.md §3.
> 4. **Sheets no longer auto-migrate, and `LAYOUT_WIDENED` no longer exists.**
>    `migrateLegacyLayoutIfNeeded()` and the manual `rebuildAllDerivedColumns()` repair have
>    both been deleted: every layout they knew about existed only on this branch, so nothing
>    in the wild needs them. "Remaining risks" #1 below is therefore wrong in its second
>    half — a sheet written under an older layout is now *not* converted at all, and the only
>    correct move is to delete it and let TiltBridge recreate it.
> 5. **The Apps Script has since run against the real runtime**, continuously, for days. The
>    "never run against the real Apps Script runtime" caveat below no longer applies.
> 6. Assorted numbers have moved: `fsFreeBytes` is ~339,968 not 356,352;
>    `senderStaleRebootSec` defaults to 90 not 75; `MISSING_READING_MINUTES` is derived from
>    the configured push interval rather than fixed at 60.
>
> Several confident conclusions in this document were later found wrong. That is recorded
> rather than edited away — the "do not re-investigate" list in KNOWN_ISSUES.md exists
> because re-deriving them was expensive.

## Build environment (set up during this work)

PlatformIO was not installed. It now is, along with two things PlatformIO failed to provision
into its own ESP-IDF virtualenv:

```bash
python3 -m venv ~/.venvs/pio
~/.venvs/pio/bin/pip install platformio

# PlatformIO's espidf venv shipped incomplete; both of these were needed:
~/.platformio/penv/.espidf-5.5.3/bin/python -m pip install "idf-component-manager~=2.0"
cd ~/.platformio/packages/framework-espidf && \
  ~/.platformio/penv/.espidf-5.5.3/bin/python -m pip install -r tools/requirements/requirements.core.txt

# Build
~/.venvs/pio/bin/pio run -e lcd_ssd1306
```

## Pre-existing bug found and fixed: FreeRTOS timer overflow above ~71 minutes

`dataSendHandler::startTimer()` used `pdMS_TO_TICKS(periodSeconds * 1000)`. On the non-SMP
kernel that macro is

```c
( ( TickType_t )( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / 1000U
```

— a 32-bit intermediate. With `configTICK_RATE_HZ = 1000` the product is
`periodSeconds x 1,000,000`, which overflows `uint32_t` above **4294 seconds**:

| Requested | Actual before fix |
|---|---|
| 1800 s | 1800 s (fine) |
| 4295 s | **0.03 s** |
| 21600 s (`queueSnapshotIntervalSec` max) | **125 s** |
| 43200 s (`legacyFermentrackPushEvery` max) | **250 s** |

Affects every timer routed through `startTimer()`, so it predates this phase — but the queue
snapshot timer inherits it, and the config clamp permits 21600. Fixed by computing ticks in
64-bit directly from seconds (`periodSeconds * configTICK_RATE_HZ`), clamped to
`portMAX_DELAY - 1`, with no millisecond intermediate.

## Queue capacity ceiling is lower than the config clamp allows

`maxQueuedRecords` clamps to 3000, which is 384 KB of records. After the web UI, LittleFS has
only ~356 KB free, and `QUEUE_MIN_FREE_BYTES` reserves 32 KB — so the practical maximum on this
hardware is roughly **2500 records**. Above that the queue hits `QUEUE_FS_FULL` and refuses to
append rather than reaching the configured cap. The clamp was chosen before the deployed UI size
was known. Consider lowering it to 2500, or deriving it from `esp_littlefs_info()` at load.

Buffer depth depends on both settings: 4 Tilts at a 30-minute interval with 1500 records is
~7.8 days; at a 10-minute interval with 1200 records it is ~2.1 days.

## RESOLVED: Google Sheets now works end to end

Verified on hardware: a 24-record backlog drained in three batches, each returning
`{"status":"ok","code":"BATCH_PROCESSED","savedRows":10,"acceptedRecordIds":[...]}` at HTTP 200 in
8–9 s, queue reached 0, `droppedOverflow` 0. Confirms the whole v2 pipeline: snapshot → flash →
batch → POST → redirect → acknowledgement → record removal → fast drain of the remainder.

It took **two independent root causes**, both pre-existing and neither caused by Phase 1:

### 1. TLS failed only for hosts with large certificate chains

`script.googleusercontent.com` (the host Apps Script redirects to) serves a **12,695-byte**
certificate chain; `script.google.com` serves 6,683 and `example.com` less again. Parsing the
large chain with static 16 KB mbedTLS buffers exhausted contiguous heap on the classic ESP32.
Small-chain hosts were unaffected, which is exactly why only Google Sheets ever broke and why
plain-HTTP Fermentrack was fine.

Isolated by pointing `userTargetURL` (same `http_request()` path) at successive hosts:

| Host | Chain | Result |
|---|---|---|
| `example.com` | small | 405 in 1.3 s — TLS fine |
| `script.google.com` | 6,683 B | 405 in 1.4 s — TLS fine |
| `script.googleusercontent.com` | 12,695 B | connect timeout, 6–60 s |

Fix: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` plus `DYNAMIC_FREE_CONFIG_DATA` and
`DYNAMIC_FREE_CA_CERT`, in `sdkconfig.defaults` and all six env files. Buffers are allocated on
demand and certificate memory is released right after parsing. `script.googleusercontent.com`
then connected in 1.4 s.

**Note:** `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` must stay at **16384**. Reducing it to 8192 was
tried and fails with `MBEDTLS_ERR_SSL_BAD_INPUT_DATA` — a 12.7 KB chain cannot fit in 8 KB.

### 2. Redirects were followed with the original method

`esp_http_client`'s built-in redirect handling re-issues the original method. RFC 9110 says a
301/302/303 answering a POST is followed with **GET**, which is what Apps Script requires: `/exec`
answers the POST with a 302 to the echo URL, and that endpoint rejects POST with 405.

Confirmed from the desktop: `curl -L --post302` → 405; following the same Location with GET →
200 and a valid `acceptedRecordIds` body.

Fix: `disable_auto_redirect = true` in `http_request()` plus a manual loop (max 3 hops) that
downgrades 301/302/303 to GET and drops the body, while preserving method and body for 307/308.
The response buffer is reset between hops so the redirect's own body is not concatenated.
This is strictly more standards-correct than the previous behaviour and applies to every target.

### Also fixed en route

- **IPv6 disabled** (`CONFIG_LWIP_IPV6=n`). `script.google.com` publishes AAAA records but the
  device only holds an IPv4 lease (`LWIP_IPV6_AUTOCONFIG` off), so esp-tls stalled on an
  unroutable v6 address for the whole timeout. `www.fermentrack.net` is IPv4-only — the asymmetry
  that first pointed at this.
- **Wi-Fi modem sleep disabled** (`esp_wifi_set_ps(WIFI_PS_NONE)` on every got-IP). This did *not*
  turn out to be the cause, but it is correct for a mains-powered bridge and was left in.
- Sheets v2 peak memory reduced: batch buffer freed before the handshake, response buffer 2048 →
  1024, `MAX_DEVICE_CONFIGS` 12 → 8.

### Hypotheses tested and excluded (recorded so they are not re-investigated)

Payload size (fails identically at 400 B and 3.4 KB) · the v2 protocol itself (legacy path failed
the same way) · BLE radio contention (pausing the scanner changed nothing) ·
`CONFIG_ESP_TLS_INSECURE`/`SKIP_SERVER_CERT_VERIFY` (both `=y`, unchanged) · TLS version (Google
accepts TLS 1.2) · hardware crypto (`MBEDTLS_HARDWARE_{AES,MPI,SHA}=y`) · Wi-Fi signal (−52 dBm,
96%, 0% loss) · endpoint/deployment (GET returned the script's own text throughout).

## Superseded investigation notes

**Proven not a regression.** With `gsheetsV2Enabled=false`, the *legacy* single-reading path
fails identically with ~200-byte payloads. Google Sheets is broken on this hardware
independently of the v2 protocol.

Symptom: TLS to `script.google.com` never completes. Errors seen are
`MBEDTLS_ERR_SSL_CONN_EOF (-0x7280)` (server closes mid-handshake after ~19 s) and
`esp-tls: Failed to open new connection in specified timeout`. Plain-HTTP Fermentrack 2 on the
same device works perfectly and instantly.

### Ruled out by measurement

| Hypothesis | Verdict |
|---|---|
| Heap / contiguous memory | **Fixed and excluded.** 40,960 B largest free block at send time; no mbedTLS alloc errors remain |
| Payload size | Excluded — fails with `queueBatchSize=1` (~400 B) exactly as with 3.4 KB |
| v2 protocol / my code | Excluded — legacy path fails identically |
| BLE radio contention | Excluded — pausing the scanner around the upload changed nothing |
| IPv6 stall | **Was a real contributor**, now fixed (see below) |
| `CONFIG_ESP_TLS_INSECURE` / `SKIP_SERVER_CERT_VERIFY` | Excluded — both `=y`, unchanged from HEAD |
| TLS version | Excluded — Google accepts TLS 1.2 (verified with `openssl s_client`) |
| Hardware crypto accel | Excluded — `MBEDTLS_HARDWARE_{AES,MPI,SHA}=y`, unchanged from HEAD |
| Wi-Fi signal | Excluded — −52 dBm, 96% quality, 0% packet loss |
| Wi-Fi modem sleep | Excluded — `esp_wifi_set_ps(WIFI_PS_NONE)` changed neither latency nor the failure |
| Endpoint / deployment | Excluded — GET returns the script's own text in 1.3–2.8 s; POST returns 302 in ~3 s |

### Still unexplained

Ping latency from the LAN is 4–84 ms with ~30 ms stddev despite a clean link, and that did not
change when power save was disabled — so something else on the device is adding round-trip
latency. Apps Script also forces **two** TLS handshakes per upload (`/exec` 302-redirects to
`script.googleusercontent.com`), doubling the exposure.

Next avenues, untried: capture the handshake with a packet trace; test HTTPS to a
non-Google host to see whether any TLS works from this device; try `esp_http_client` with
`disable_auto_redirect = true` and follow the 302 manually.

**No data is being lost meanwhile** — readings accumulate in the persistent queue (56 at the time
of writing, 0 dropped) and will upload once this clears.

## Fixed during hardware bring-up

- **IPv6 disabled** (`CONFIG_LWIP_IPV6=n`, in `sdkconfig.defaults` and all six env files).
  `script.google.com` publishes AAAA records; the device only ever holds an IPv4 lease
  (`LWIP_IPV6_AUTOCONFIG` off), so esp-tls stalled on an unroutable v6 address for the whole
  timeout. `www.fermentrack.net` is IPv4-only, which is what isolated it. After this fix the v2
  TLS connection establishes and the error moved from `HTTP_CONNECT` to `HTTP_EAGAIN`.
  Pre-existing at HEAD, not introduced here.
- **`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` kept at 16384.** Reducing it to 8192 produced
  `MBEDTLS_ERR_SSL_BAD_INPUT_DATA` — Google's certificate chain does not fit in 8 KB. The earlier
  alloc failures were my own code's fault, fixed in `gsheets_v2.cpp` instead.
- **Sheets v2 peak memory cut**: the batch buffer is freed before the TLS handshake, the response
  buffer is 1024 not 2048, and `MAX_DEVICE_CONFIGS` is 8 not 12 (~2 KB of static RAM).
- **Per-target failure counters** (`SendTargetStatus::consecutiveFailures`). A single shared
  counter was reset by any healthy target, hiding one failing every cycle. `/api/sender/` now
  reports the worst plus a `targetFailures` breakdown — this is what cleanly showed
  `{"google_sheets": 2}` while everything else was fine.
- **Circuit breaker** (spec §16, previously planned but never implemented): after 5 consecutive
  failures a target's retry interval doubles per failure, capped at 30 min, across all eleven
  targets. For Sheets v2 it is scoped to the failure path so the fast backlog drain is unaffected.
- **Heartbeat refreshed between targets** in `process()`. After an outage every timer can be due
  at once, and one pass could exceed `senderStaleRebootSec` and trigger a spurious recovery
  reboot. Completing a target is progress; a target wedged inside its own HTTP call still never
  reaches the next heartbeat, which is the case the monitor must catch.
- `GSHEETS_V2_TIMEOUT_MS` 15000 → 60000 and `senderStaleRebootSec` default 75 → 90, sized for the
  two-handshake Apps Script round trip.

## Deployed to hardware

Flashed to an ESP32-D0WD-V3 rev 3.1 (4 MB, MAC `AA:BB:CC:DD:EE:FF`, env `esp32_headless`,
`tiltbridge.local` / 192.168.1.x) with four live Tilts. Firmware and filesystem both written and
hash-verified. See `10-acceptance-tests.md` for the results table — T2 and T7 pass, T1/T5/T6/T8
partial, T3/T4 need physical intervention.

Flashing notes worth keeping:
- Long serial **reads** are unreliable on this board/cable (three failed attempts at a full
  partition dump, at 460800, 115200-stub and 115200-no-stub). Writes are completely reliable.
  Back up config over HTTP (`GET /api/settings/json/`), not via `esptool read_flash`.
- `uploadfs` erases the whole LittleFS partition, which destroys `tiltbridgeConfig.json`. The
  Fermentrack 2 registration in it **cannot be restored through the settings API**, because
  `processFermentrackSettings()` deliberately clears `fermentrackDeviceID`/`fermentrackAPIKey` and
  forces re-registration — which would create a duplicate device upstream. The safe procedure is
  to seed `data/conf/tiltbridgeConfig.json` into the image before `uploadfs`, which requires
  temporarily adding `"conf"` to `PRESERVE_ENTRIES` in `tools/build_ui.py` (that script wipes
  `data/` except `wifiui`). Revert both afterwards.

### Regression caught by hardware testing (fixed)

`send_status_to_fermentrack_2()` serialised into a fixed `char payload[2048]` stack buffer.
Adding device identity and RSSI aggregates to `to_json()` pushed the multi-Tilt payload past it
and `serializeJson` truncated silently, so Fermentrack 2 returned
`400 JSON parse error … column 2049`. Now measured with `measureJson` and heap-allocated, with an
explicit truncation check. The *legacy* Fermentrack buffer had already been guarded via the
`legacy_keys` branch; this second buffer was missed because it consumes `tilt_to_json()` directly.
`idf_json_send_response()` was checked and already sizes dynamically, so the web API was unaffected.

### Filesystem headroom is tighter than planned

`fsFreeBytes` after the larger UI is 356,352 (~348 KB), not the ~800 KB the original budget
assumed. *(Now ~339,968 — the UI has grown again since.)* A full 1500-record queue is 192,000 B, leaving ~164 KB. It fits, but re-check if the UI
grows; the `QUEUE_MIN_FREE_BYTES` 32 KB floor guard is doing more work than anticipated.

## ⚠ BLE provisioning is silently disabled — needs a decision

Discovered while reviewing build output. Not caused by this phase's work.

`sdkconfig.defaults:28` sets `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y`. The updated
`esp_wifi_config` component **renamed that symbol** to
`CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING`. Because `sdkconfig.defaults` still names the old
symbol, kconfgen ignores it as unknown and the replacement falls back to its default — off. Every
regenerated `sdkconfig.*` now contains:

```
# CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING is not set
```

So BLE Wi-Fi provisioning is off on all six targets, where it was previously on, and the
`.prov_ble = { .device_name = "TiltBridge-{id}" }` block in `wifi_setup.cpp` is inert.

**Deliberately not fixed.** Enabling it pulls in the IDF `wifi_provisioning` manager, which takes
ownership of the BLE stack — precisely the conflict `main.cpp:117-121` warns about ("Otherwise it
claims ownership of NimBLE and tilt_scanner.init() later fails with controller-init errors").
Flipping it could break Tilt scanning, so it is the user's call. The one-line fix, if wanted, is
to rename the symbol in `sdkconfig.defaults` and then verify BLE scanning still works.

Related: the six `sdkconfig.*` files show large diffs (~400 lines each). Almost all of it is
cosmetic `# default:` comment lines emitted by the newer kconfgen. The only semantic changes are
the provisioning symbol above and `CONFIG_NIMBLE_SVC_GAP_APPEARANCE` `0` → `0x0`.

## Repository HEAD moved mid-session

The branch was at `7a5d7e8` when this work started and is now at `5a28409`
("Merge pull request #293 … refactor/wifi_merge"). That upstream WiFi refactor is what introduced
both the `.ble`/`.prov_ble` mismatch below and the provisioning symbol rename above. It also added
the `GoogleSheets/` directory. No commits were made by this work — everything is in the
working tree, some of it staged by something outside this session.

## Pre-existing breakage fixed to make the tree build at all

`src/wifi_setup.cpp:219` used `.ble = { .device_name = ... }`, but the pinned
`thorrak/esp_wifi_config` component calls that member **`prov_ble`**. HEAD did not compile before
this was changed. The component is pinned to a moving `version: main` in `src/idf_component.yml`,
so this can drift again — consider pinning a commit.

## Stage status

| Stage | Spec | Status | Build |
|---|---|---|---|
| 1–2 Inspection | §28.1–2 | **done** — `01-findings-identity-and-lock-audit.md` | n/a |
| 3 Sender diagnostics | §18 | **done** | clean |
| 4 Replace `send_lock` | §17 | **done** | clean |
| 5 Stale detection + recovery | §1, §19 | **done** | clean |
| 6 Device/MAC config | §2, §3, §21 | **done** (firmware; UI pending) | clean |
| 7 RSSI aggregation | §4, §5 | **done** (firmware; UI pending) | clean |
| 8 Persistent queue | §7, §9–12, §25, §26 | **done** | clean |
| 9 Queue settings | §8, §22 | **done** | clean |
| 10–11 GSheets v2 + ack | §6, §13–15 | **done** | clean |
| 12 Web UI | §20–24 | **done** | `npm run build` clean |
| 13 Acceptance tests | §27 | **partial** — T6 (recovery reboot) verified on hardware 2026-08-15, see DEVELOPMENT.md §8.1 | — |
| Apps Script | §15 | **done** — see below | mock-tested |

**All six defined environments build clean** (`pio run`, 6 succeeded in 5m48s):

| Environment | RAM | Flash |
|---|---|---|
| `lcd_ssd1306` | 19.7% | 44.0% |
| `esp32_headless` | — | — |
| `esp32_large_tft` | — | — |
| `esp32_small_tft` | — | — |
| `esp32_s3_oled` | 17.3% (56,568 B) | 42.5% (1,394,239 B) |
| `esp32_s3_small_tft` | 17.6% (57,688 B) | 44.4% (1,455,111 B) |

Both architectures are covered, which matters because `send_json_str.cpp:55` carries an
S3-specific `constexpr` workaround.

Note: `sdkconfig.esp32_s3_tdisplay` exists but there is **no** `[env:esp32_s3_tdisplay]` in
`platformio.ini` — that board was folded into `esp32_s3_small_tft` ("auto-detects M5StickC S3
and S3 T-Display"). The stale sdkconfig is pre-existing and harmless.

## What landed, file by file

**New files**
- `src/sender_health.{h,cpp}` — `SenderHealthMonitor` (heartbeat, lock, per-target success
  tracking, stale detection, recovery record, monitor task) and the `SenderLock` RAII guard.
- `src/rssi_stats.h` — `RssiStats` interval accumulator + `rssiQuality()` bands.
- `src/device_config.{h,cpp}` — `DeviceConfigStore`, `/littlefs/conf/devices.json`, colour
  fallback for every resolved accessor.
- `src/time_sync.{h,cpp}` — minimal SNTP, `time_is_valid()`, `utc_now()`, ISO-8601 formatting.
  Rejects any time before 2025-01-01 so a bogus sync cannot mark records valid.
- `src/queue/reading_record.{h,cpp}` — 128-byte packed `QueuedReading` (`static_assert`ed),
  CRC-32 validation, CRC-16 device hash, `"A91F2C-6A72-00000452"` id formatting.
- `src/queue/reading_queue.{h,cpp}` — append-only 8 KB segments + 8-byte-entry journal, boot
  scan with torn-tail truncation, FIFO `peekBatch`, `acknowledgeId`, overflow drop with a
  persisted counter, segment compaction, `clear()`.
- `src/targets/gsheets_v2.cpp` — batched schemaVersion 2 upload, `acceptedRecordIds` handling
  for all six failure modes, fast backlog drain on progress.

**Modified**
- `src/sendData.{h,cpp}` — heartbeat at the top of `process()`; gate switched from
  `is_wifi_connected()` to `network_is_usable()`; all eight in-file senders converted to
  `SenderLock`; `sendTargetNames[]` moved here from `http_server.cpp`; per-target HTTP timeout
  macros; device-aware name resolution and `isEnabled` skips; **null-`doclongurl` crash fixed**.
- `src/targets/{legacy_fermentrack,fermentrack_2,mqtt}.cpp` — converted to `SenderLock`.
- `src/targets/send_json_str.cpp` — brackets `esp_http_client_perform()` with
  `noteRequestStart/End`.
- `src/wifi_setup.{h,cpp}` — `network_is_usable()`, `wifi_flag_disagreements()`,
  throttled `reconnectWiFi()`, `.ble`→`.prov_ble` fix.
- `src/tilt/tiltHydrometer.{h,cpp}` — cached canonical `m_device_id`, `setAddress()`,
  `last_update_age_ms()`, `rssi_stats`, RSSI captured before the version-code early return,
  calibration sourced through `device_config`, new `to_json` keys behind `if (!legacy_keys)`.
- `src/tilt/tiltScanner.{h,cpp}` — `g_last_tilt_advert_ms` atomic for BLE liveness;
  `isEnabled` skip in the legacy payload.
- `src/jsonconfig.{h,cpp}` — `senderRecoveryEnabled`, `senderStaleRebootSec` (clamped 60–600).
- `src/http_calibration.{h,cpp}` — optional `deviceId` on all point/coefficient operations,
  `calibrationFilename()` helper, per-device coefficient storage.
- `src/http_server.{h,cpp}` — `/api/sender/`, `/api/devices/` GET, `/api/devices/` PUT,
  `/api/devices/delete/` POST, `debugFreezeSender` action under `-D TB_DEBUG_FREEZE=1`.
- `src/main.cpp` — `device_config.load()`, `sender_health.loadRecoveryRecord()` (after
  `nvs_flash_init()`), `sender_health.init()`, `sender_health.startMonitorTask()`.

## New HTTP API surface

```
GET  /api/sender/            sender state, heartbeat age, lock, per-target success ages,
                             wifiFlagDisagreements, lastRecovery
GET  /api/devices/           { schemaVersion, maxDevices, devices[] }
PUT  /api/devices/           upsert one device by deviceId
POST /api/devices/delete/    { deviceId } -> revert to colour config
GET  /api/queue/             counts, oldest age, storage %, overflow, timeValid,
                             uploadStatus, lastUploadSuccessAgeSec
POST /api/queue/actions/     { "action": "sendBacklogNow" }
                             { "action": "clearQueue", "confirm": true }  (400 without confirm)
POST /api/actions/           + "debugFreezeSender" (only with -D TB_DEBUG_FREEZE=1)

PUT  /api/settings/controller/   + queueSnapshotIntervalSec, maxQueuedRecords, queueBatchSize,
                                   offlineQueueEnabled, senderRecoveryEnabled, senderStaleRebootSec
PUT  /api/settings/targets/      + gsheetsV2Enabled (with the Google Sheets keys)
```

## The field-failure fix, and how to confirm it

The prime suspect is documented in `00-OVERVIEW.md`: `process()` was gated solely on
`wifi_cfg_is_connected()`, a flag owned by the external `esp_wifi_config` component. A stale-false
flag there stops every outbound target while BLE and the web server keep running — matching all
six reported symptoms including "reset fixes it".

Three independent mitigations are now in place:

1. `network_is_usable()` verifies the netif directly before believing the flag, and counts
   disagreements. **If `/api/sender/` shows `wifiFlagDisagreements > 0` in the field, the
   diagnosis is confirmed.**
2. `reconnectWiFi()` no longer calls `wifi_cfg_connect()` on every ~10 ms loop iteration.
3. An independent monitor task (core 0, priority 2) restarts the device when the sender heartbeat
   exceeds `senderStaleRebootSec` (90 s default) **and** BLE is still receiving **and** the network
   is usable **and** uptime > 180 s. Repeated HTTP failures alone never reboot.

> **CORRECTION (2026-08-15): mitigation 3 never worked until it was fixed.**
>
> The `uptime > 180 s` guard was written as
> `if (uptimeSeconds(true) < RECOVERY_GRACE_SEC) return;`, and `uptimeSeconds()` returns the
> seconds component of a d/h/m/s breakdown — a value capped at 59. It can never reach 180, so
> `checkForRebootCondition()` returned early on **every call** and the reboot could not fire.
>
> This also means `staleEvents: 0` and `lastRecovery: null` were worthless as evidence of
> health for as long as the bug existed: those counters could not have read anything else.
>
> Now derived from `uptimeTotalSeconds()` and verified on hardware — sender wedged with
> `TB_DEBUG_FREEZE`, `rst:0xc (SW_CPU_RESET)` 90.7 s later, recovery record intact across the
> restart. Mitigations 1 and 2 were unaffected.

## Deliberate design notes worth keeping

- `m_lock` is `xSemaphoreCreateBinary()`, not `xSemaphoreCreateMutex()`, precisely so the monitor
  task can force-release a lock it does not own. A priority-inheritance mutex asserts on that.
- The recovery record lives in `RTC_NOINIT_ATTR` (free, survives software reset) **and** NVS (one
  write per event, survives power loss).
- BLE liveness is a lock-free atomic published by the BLE callback. The monitor must never iterate
  `m_tilt_devices` — the BLE task mutates it.
- New `to_json` keys are confined to `if (!legacy_keys)` because the legacy Fermentrack payload is
  sized by `TILT_ALL_DATA_SIZE`.

## Known gaps / follow-ups

- **ArduinoJson narrow-type guards swallow wildly out-of-range values.** `is<uint16_t>()` is
  false for `100000` and `is<uint8_t>()` is false for `300`, so such a value is silently ignored
  and the PUT still returns 200. Values inside the type but outside the allowed range do warn and
  fail correctly. This matches the pre-existing `smoothFactor` behaviour, so it was left as-is —
  the UI clamps client-side. Widen the guards to `is<int>()` if a silent no-op is unacceptable.
- One rejected queue value fails the **whole** controller PUT (`failCount > 0` skips
  `config.save()`). Pre-existing contract of that handler, deliberately preserved.
- `m_tilt_devices` is iterated from `loopTask` without a lock while the BLE task mutates it. This
  is pre-existing, not introduced here, and is called out in `06-persistent-queue.md`.
- Build coverage is complete: all six environments pass. No target-specific gaps remain.
- `src/idf_component.yml` pins `esp_wifi_config` to a moving `version: main`. That is what broke
  `.ble` → `.prov_ble`. Consider pinning a commit hash.
- No acceptance test (§27 / stage 13) has been run yet — all of them need hardware.

Resolved during implementation, recorded so it is not re-investigated:
- `maxQueuedRecords` **is** honoured live: `ReadingQueue::append()` compares against
  `config.maxQueuedRecords` on every push, so a settings change takes effect immediately with no
  re-init needed.
- SNTP now exists (`src/time_sync.{h,cpp}`), started from the got-IP event, so `TimestampValid`
  becomes true once a sync lands. Note this also means brewstatus and MQTT, which call
  `std::time(0)` directly, will start emitting real times instead of 1970-based ones — a fix, but
  a visible behaviour change worth putting in the changelog.

## Apps Script — `GoogleSheets/post_tilt.gs`

Not the stock TiltBridge script: a customised wine-fermentation script (rolling 4-hour averages,
Monitoring tab, System Log, gap detection, 9-column layout). Integration constraints are
catalogued in `APPS_SCRIPT_PROTOCOL.md` §5.

A `schemaVersion === 2` batch path was added, the sheet layout was widened to expose the v2
diagnostics, and the legacy/batch row-writing paths were unified. 2698 → 3930 lines (the unify
step removed more than the layout added). Verified by executing the real file against a
hand-written mock of `SpreadsheetApp` / `PropertiesService` / `LockService` / `ContentService` /
`Utilities` — 29 scenarios across four harnesses, all passing.
~~**It has never run against the real Apps Script runtime.**~~ **It has since** — deployed and
serving a live fermentation continuously. There is also a mock harness in the repo now,
`node GoogleSheets/test/run_tests.js`, so layout and threshold changes are checkable without
deploying.

### Wine sheet layout (`LAYOUT_VERSION = wine-layout-v13-v2-diagnostics`) — SUPERSEDED

> **Two revisions out of date.** The layout is now **twelve** columns at
> `wine-layout-v16-quality-as-fill`, documented in [../APPS_SCRIPT.md](../APPS_SCRIPT.md) §3.
> Two changes since:
>
> - **v15** narrowed the diagnostic block from K–S to K–M. Smoothed SG went (column B already
>   *is* the smoothed value), the five RSSI columns collapsed to one average with the detail in
>   a note, and the per-row MAC went (it never changes; a change is now a note plus a
>   `DEVICE_CHANGED` log entry).
> - **v16** retired the **Average quality** column entirely. It is now the background fill on
>   the two average cells E:F, bold, with the text and gap detail in a note on E — a column
>   that was blank on ~7 rows in 8 and only described its neighbours.
>
> The table below is kept as the record of what v13 looked like.

| Col | Field | | Col | Field |
|---|---|---|---|---|
| A | Date and time (**capture**, never upload) | | K | Raw SG (`SG_Raw`) |
| B | SG (final calibrated + smoothed) | | L | Smoothed SG (`SG_Smoothed`) |
| C | Temperature °C | | M | RSSI dBm |
| D | *(separator)* | | N–P | RSSI avg / min / max |
| E–F | 4-hour avg SG / °C | | Q | RSSI samples |
| G | Average quality | | R | Device (MAC), text format |
| H–I | Previous day avg SG / °C | | S | Record id, text format |
| J | *(separator)* | | | |

`DATA_COLUMN_COUNT = 19`. **A–I keep their exact former positions**, so charts, rolling averages,
daily averages, gap detection and new-day shading needed no changes. R and S are text-formatted so
a MAC or `A91F2C-6A72-…` id is never coerced to a number or date.

Charts moved from `CHART_COLUMN = 10` (column J) to **21 (column U)**, one clear column past S.
A test asserts no wine sheet has data at or past column 21.

### Unified writer

`appendMeasurementRow(spreadsheet, state, measurement)` is now the single place gap detection, the
interval throttle, rolling averages, previous-day averages and new-day shading exist. Both callers
are thin adapters; the legacy one passes `receivedAt` as the capture time, which degenerates the
capture-to-capture gap comparison back to the original request-to-request one. `migrateRows()`,
`shouldWritePeriodicAverage()` and the ~280-line inline legacy writer were deleted as dead.

### Migration of existing sheets

`migrateLegacyLayoutIfNeeded` preserves A/B/C (raw date/SG/°C, with the oldest °F-layout detected
and Celsius pulled from its old column D), clears D–S including stale notes and fills, and logs
`WARNING / LAYOUT_WIDENED`. "Detect and refuse" was rejected because `prepareSheet()` would then
either leave stale headers over reinterpreted columns or append 19-column rows under 9-column
rows — both silently misalign the sheet.

`WEBAPP_LOCK_WAIT_MS` stays 6000 (its log message promises "within 6 seconds"); v2 uses a separate
`WEBAPP_BATCH_LOCK_WAIT_MS = 10000`. `_processed_ids` remains the dedup source of truth.

### `MISSING_READING_MINUTES` raised 30 → 60 — SUPERSEDED

> **Now derived, not fixed at 60.** A constant was still wrong for any cadence but the one it
> was chosen for: at a 30-minute push interval a *single* missed reading hit exactly 60 and
> flagged the wine MISSING. It is now `max(60, EXPECTED_READING_INTERVAL_MINUTES * 2.5)`,
> alongside the average-quality thresholds which had the identical flaw — a perfect window sat
> exactly on the COMPLETE boundary, so jitter read INCOMPLETE and one gap read INSUFFICIENT
> DATA. See [../APPS_SCRIPT.md](../APPS_SCRIPT.md) §2. The reasoning below still explains why
> the threshold must exceed the capture cadence.

Done directly, not left as advice, because v2 is now the firmware default and the old value was
guaranteed to misfire: under v2 the threshold is measured against **capture** time, and with a
30-minute snapshot interval the newest capture in a batch is always ~30 minutes old on arrival.
60 tolerates the default interval plus one missed snapshot. It must stay comfortably above the
device's `queueSnapshotIntervalSec`. Verified this does not disturb the gap tests: a 4-hour gap
(240 min) still fires, a 30-minute backlog still does not.

### Deliberate deviation from the spec, with rationale

A reading whose `SG` or `Temp` is missing or non-numeric is logged `INVALID_READING`, recorded as
`REJECTED_INVALID`, and **acknowledged** rather than retried. Strict at-least-once would retry it
forever and permanently pin a device queue slot, and it can never become valid. Genuine
spreadsheet write failures still follow the retry contract exactly.

### Remaining risks

1. **Existing sheets need resetting, or will auto-migrate.** With v2 as the default, every existing
   wine sheet re-runs `prepareSheet` on the first request after deploy and logs `LAYOUT_WIDENED`.
   Deleting the sheets and letting TiltBridge recreate them is cleaner.
2. **One-off ordering glitch when switching modes.** The first v2 batch can append rows dated
   earlier than the last legacy row, briefly breaking the chronological assumption. Self-corrects
   after that batch. Moot if the sheets are reset.
3. `BATCH_MAX_READINGS = 40` caps one request. Raising the firmware's `queueBatchSize` above 40
   requires raising this too.
4. `createOrRefreshCharts` is still only called from `initialSetup()`, so a sheet auto-created by a
   POST gets its charts on the next manual setup run. Pre-existing; the `NEW_WINE_SHEET_CREATED`
   log already says so.
5. ~~Wall-clock time for a 20-row batch against the real runtime versus the firmware's 15 s
   timeout still needs one live measurement.~~ **Measured.** Splitting 52 uploads at the 302:
   the Apps Script leg averages **10.6 s** (range 6–22 s) and the device's own leg 2.3 s. The
   firmware timeout is now 60 s, not 15, so there is margin — but note `senderStaleRebootSec`
   is 90 s, so a request that hangs to its full timeout leaves only 30 s before the health
   monitor treats the stalled heartbeat as grounds for a reboot. Anything that makes the script
   slower eats that margin. Breakdown and the four worthwhile reductions:
   [../APPS_SCRIPT.md](../APPS_SCRIPT.md) §5.
