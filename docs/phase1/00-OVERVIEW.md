# TiltBridge Beta 5 Enhanced Fork — Phase 1 Plan Overview

Base: `v2.0.0-beta5` (branch `master`, tag flag `PIO_SRC_TAG="2.0.0-beta5"`).
Framework: **ESP-IDF** (not Arduino) via PlatformIO `espressif32@6.13.0`.
Filesystem: **LittleFS**, mounted at `/littlefs`, partition `0x330000` size `0xD0000` (851,968 B ≈ 832 KB).
Web UI: Vue 3 + Pinia + Tailwind in `tiltbridge_web_ui/`, built by `tools/build_ui.py` into `data/wifiui/` (currently ~24 KB gzipped) and flashed to LittleFS.

## Plan files, in implementation order

| # | File | Spec sections |
|---|------|---------------|
| 1–2 | `01-findings-identity-and-lock-audit.md` | 28.1, 28.2 (inspection results) |
| 3–4 | `02-sender-diagnostics-and-locking.md` | 17, 18 |
| 5 | `03-sender-recovery.md` | 1, 19 |
| 6 | `04-device-config.md` | 2, 3, 21 |
| 7 | `05-rssi-aggregation.md` | 4, 5 |
| 8 | `06-persistent-queue.md` | 7, 9, 10, 11, 12, 25, 26 |
| 9 | `07-queue-settings.md` | 8, 22 |
| 10–11 | `08-gsheets-v2-protocol.md` | 6, 13, 14, 15 |
| 12 | `09-web-ui.md` | 20, 21, 22, 23, 24 |
| 13 | `10-acceptance-tests.md` | 27 |
| ref | `APPS_SCRIPT_PROTOCOL.md` | 15 (server-side contract to document) |

Build after each numbered file lands. See "Build" below.

## Headline finding: the most likely cause of the observed freeze

Symptoms (spec §1): outbound sending to *all* targets stops after hours; `tiltbridge.local`
stays up; Detected Tilts keeps updating; BLE values keep changing; reset fixes it.

`dataSendHandler::process()` (`src/sendData.cpp:140`) is gated on a single condition:

```cpp
void dataSendHandler::process() {
    if (is_wifi_connected()) {   // src/wifi_setup.cpp:255 -> wifi_cfg_is_connected()
        send_to_legacy_fermentrack();
        ... every other target ...
    }
}
```

`is_wifi_connected()` returns `wifi_cfg_is_connected()` — a state flag owned by the external
`thorrak/esp_wifi_config` component. **If that flag goes false (or stale) while the STA netif is
actually up, every outbound target stops permanently and nothing else in the firmware is
affected**: BLE scanning runs from `loopTask` which keeps looping, and the HTTP server runs in
its own httpd task. That is an exact match for all six reported symptoms, including "reset
fixes it".

Corroborating evidence in the same file:

- `src/wifi_setup.cpp:170` — the `WIFI_CFG_EVT_DISCONNECTED` subscription is **commented out**,
  so local state tracking around disconnects is already known-partial.
- `reconnectWiFi()` (`src/wifi_setup.cpp:249`) calls `wifi_cfg_connect(NULL)` on *every* loop
  iteration (~every 10 ms) whenever `wifi_cfg_is_connected()` is false. If the flag is stale-false
  while the link is fine, that hammers the library's connect path continuously.
- Recent commit `61c6ee6 "Check for WiFi status in data send loop"` is what introduced this gate.
  The reported failure post-dates it.

`send_lock` leaking is a *secondary* candidate. Audited in `01-findings-…`: every current path
does pair `send_lock = true` with `send_lock = false`, and all sends run from the single
`loopTask`, so a leak requires an exception/abort mid-send rather than an ordinary early return.
Per spec §1 we do not assume replacing `send_lock` fixes anything — we fix it *and* add
independent progress detection.

### What Phase 1 therefore does about it

1. Stop trusting a single library flag. Introduce `network_is_usable()` that ORs
   `wifi_cfg_is_connected()` with a direct check (`esp_netif_is_netif_up("WIFI_STA_DEF")` **and**
   non-zero IP) — the same technique `send_json_str.cpp:88` already uses privately.
2. Rate-limit `reconnectWiFi()` so it cannot spin.
3. Replace `send_lock` with a real mutex + RAII guard (§17) so no path can leak it.
4. Add a sender heartbeat updated **only** from the outbound path (§18), and an independent
   monitor task that reboots when the outbound loop stops progressing while BLE + Wi-Fi are
   alive (§19). This catches the observed failure whatever its root cause.

## Architectural decisions locked for this phase

- **Device identity** = BLE address string, uppercase, colon-separated (`88:C2:55:AC:26:81`).
  `tiltHydrometer::m_address` (a `NimBLEAddress`) already carries it; `to_json()` already emits
  `mac`. Config gains a MAC-keyed table with color-keyed fallback (§3).
- **No new time source assumptions.** There is currently **no SNTP anywhere** in the tree
  (`grep sntp` → nothing; `std::time(0)` is used raw by brewstatus and MQTT). Phase 1 adds a
  minimal SNTP client purely so §12 timestamps can be real, plus a `timestampValid` flag that is
  false until first sync. No RTC (§29).
- **Queue storage**: append-only segmented log on LittleFS under `/littlefs/queue/`, one
  fixed-size record per append, CRC per record, trailing partial record discarded on load (§26).
  Never rewrite the whole queue (§25).
- **Google Sheets v2** is a *separate* code path (`src/targets/gsheets_v2.cpp`) selected by a new
  config flag. The existing `send_to_google()` stays byte-for-byte functional (§13).
- **Gravity**: do not touch `tiltHydrometer::set_values()` smoothing (§6). Records carry
  `gravity` = existing `cal_smooth_gravity` (the normal final value), plus `gravityRaw` =
  `latest_gravity` and `gravitySmoothed` = `uncal_smooth_gravity`, all three of which already
  exist as members — no algorithm change, just exposure.

## Key file inventory

| Concern | Files |
|---|---|
| Sender core + lock | `src/sendData.{h,cpp}` |
| Per-target senders | `src/targets/{legacy_fermentrack,fermentrack_2,mqtt}.cpp`, `send_to_*` in `sendData.cpp` |
| HTTP transport | `src/targets/send_json_str.{h,cpp}` |
| BLE identity | `src/tilt/tiltScanner.{h,cpp}`, `src/tilt/tiltHydrometer.{h,cpp}` |
| Config | `src/jsonconfig.{h,cpp}`, `src/JsonKeys.h` |
| Web API | `src/http_server.{h,cpp}`, `src/idf_json_utils.cpp` |
| Calibration (color-keyed today) | `src/http_calibration.cpp` |
| Wi-Fi state | `src/wifi_setup.{h,cpp}` |
| Main loop | `src/main.cpp` |
| Filesystem | `src/filesystem.{h,cpp}` |
| UI | `tiltbridge_web_ui/src/{components,stores}/` |

New files this phase:

```
src/sender_health.{h,cpp}      # heartbeat, lock state, monitor task
src/device_config.{h,cpp}      # MAC-keyed device config + color fallback
src/rssi_stats.h               # interval accumulator (overflow-safe)
src/queue/reading_queue.{h,cpp}   # persistent segmented queue
src/queue/reading_record.h        # on-flash record layout + CRC
src/time_sync.{h,cpp}          # minimal SNTP + timestampValid
src/targets/gsheets_v2.cpp     # batch protocol + ack handling
```

## Build

**PlatformIO is not installed on this machine** (`pio` not on PATH, no `~/.platformio`, no
`.pio/` in the repo). Builds cannot be verified locally until a toolchain exists. Options:

```bash
python3 -m venv ~/.venvs/pio && ~/.venvs/pio/bin/pip install platformio
~/.venvs/pio/bin/pio run -e lcd_ssd1306      # first run downloads ~1–2 GB of toolchain
```

Build targets to check (each has its own `sdkconfig.*`): `lcd_ssd1306`, `esp32_headless`,
`esp32_small_tft`, `esp32_large_tft`, `esp32_s3_oled`, `esp32_s3_small_tft`, `esp32_s3_tdisplay`.
`lcd_ssd1306` is the fastest representative for iteration; add one S3 target before finishing,
because S3 has a `constexpr` quirk already worked around in `send_json_str.cpp:55`.

UI build needs Node: `cd tiltbridge_web_ui && npm ci && npm run build` (invoked automatically by
`tools/build_ui.py` on `pio run -t buildfs`).

## Out of scope reminders (§29)

No battery estimation, no RSSI history charts, no persistent Fermentrack backlog, no RTC, no
multi-stage Wi-Fi recovery ladder, no hardware watchdog, no scheduled preventive reboot, no
persistent diagnostic logs, no downloadable reports.
