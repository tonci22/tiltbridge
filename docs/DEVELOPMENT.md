# Building, flashing and testing TiltBridge

Everything needed to go from a clean checkout to firmware running on a board, plus how to test the
parts that do not live on the ESP32 (the web UI and the Google Apps Script).

---

## 1. Prerequisites

| Tool | Why | Notes |
|---|---|---|
| **PlatformIO Core 6.x** | builds and flashes the firmware | pulls the ESP-IDF, toolchains and `esptool.py` on first run |
| **Python 3.12** | runs PlatformIO | this machine's default `python3` is 3.14, which PlatformIO 6.1.x does not declare support for; 3.12 is installed alongside it and is what the venv below uses |
| **Node.js + npm** | builds the Vue web UI | only needed for `--target buildfs`; a plain firmware build skips it |

### Installing PlatformIO Core

PlatformIO lives in its own virtualenv at `~/.platformio/penv` and is **not** on `PATH`. The
official installer puts it there; doing it by hand is equivalent:

```bash
/opt/homebrew/bin/python3.12 -m venv ~/.platformio/penv
~/.platformio/penv/bin/python -m pip install --upgrade pip
~/.platformio/penv/bin/python -m pip install platformio
~/.platformio/penv/bin/pio --version          # PlatformIO Core, version 6.1.19
```

Because it is not on `PATH`, every command below spells out `~/.platformio/penv/bin/pio`. If you
would rather type `pio`, add this to your shell profile:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

The toolchains, the ESP-IDF framework and the `espressif32` platform are downloaded into
`~/.platformio/packages` and `~/.platformio/platforms` on the first build. That first build takes
several minutes; later ones are about a minute.

---

## 2. Board environments

There is no `default_envs`, so **every command must name an environment with `-e`**. The
`[platformio]` section has some `default_envs` lines but they are all commented out on purpose —
uncommenting one breaks CI.

| Environment | Board |
|---|---|
| `esp32_headless` | plain ESP32, no display or buttons |
| `lcd_ssd1306` | small OLED board |
| `esp32_large_tft` | Lolin ESP32 + ILI TFT |
| `esp32_small_tft` | 135x240 SPI TFT — auto-detects M5StickC Plus/Plus2 and TTGO T-Display |
| `esp32_s3_oled` | Heltec WiFi Kit 32 V3 |
| `esp32_s3_small_tft` | S3 small TFT — auto-detects M5StickC S3 and S3 T-Display |

`esp32_headless` is the quickest compile check: it has the fewest display dependencies, so it
catches ordinary C++ mistakes without building any of the graphics stack.

---

## 3. Building

```bash
# Compile one environment
~/.platformio/penv/bin/pio run -e esp32_headless

# Compile several at once
~/.platformio/penv/bin/pio run -e esp32_headless -e esp32_small_tft

# Compile every environment (slow — use before a release, not per-edit)
~/.platformio/penv/bin/pio run

# Force a full rebuild after changing platformio.ini, sdkconfig or a build flag
~/.platformio/penv/bin/pio run -e esp32_headless --target clean
```

A successful build ends with a size report and `[SUCCESS]`:

```
RAM:   [==        ]  18.4% (used 60364 bytes from 327680 bytes)
Flash: [====      ]  41.0% (used 1343959 bytes from 3276800 bytes)
```

Flash is measured against the 3.2 MB app partition from `4mb_no_ota.csv`, so there is plenty of
headroom — but the partition table has **no OTA slot**, which is why `DISABLE_OTA_UPDATES=1` is set.

### Watch out for warnings

Warnings do not fail the build. After touching C++, check the ones in your own files:

```bash
~/.platformio/penv/bin/pio run -e esp32_headless 2>&1 | grep -iE "warning|error" | grep -i "yourfile"
```

The ESP-IDF's own `Kconfig` files emit `'default 0' is not a valid bool value` notes on every build.
Those are upstream noise, not something to fix here.

---

## 4. The connected board

A CP2102-based ESP32 is currently attached. Ask PlatformIO rather than guessing:

```bash
~/.platformio/penv/bin/pio device list
```

```
/dev/cu.usbserial-0001
----------------------
Hardware ID: USB VID:PID=10C4:EA60 SER=0001 LOCATION=0-1.1.3.4
Description: CP2102 USB to UART Bridge Controller
```

- **Port:** `/dev/cu.usbserial-0001`
- **Chip:** CP2102 USB-to-UART bridge (`10C4:EA60`) — the usual ESP32 dev-board serial chip

On macOS always use the `/dev/cu.*` node, never `/dev/tty.*`. The `tty.*` node blocks on open
waiting for DCD and will hang esptool.

Ignore `/dev/cu.Bluetooth-Incoming-Port`, `/dev/cu.debug-console` and `/dev/cu.IBIZA-PORT` — those
are built-in macOS devices, not your board.

`tools/get_port.py` already sets `UPLOAD_PORT` to the glob `/dev/cu.usbserial-*` on macOS, so
uploads usually find the board with no `--upload-port` at all. Pass it explicitly when more than
one USB-serial adapter is plugged in.

---

## 5. Flashing

There are **two separate images** and they are uploaded by different commands. Forgetting the
second one is the classic mistake: the firmware boots but serves no web UI.

### Firmware

```bash
~/.platformio/penv/bin/pio run -e esp32_headless --target upload
```

### Filesystem (the web UI)

```bash
~/.platformio/penv/bin/pio run -e esp32_headless --target buildfs --target uploadfs
```

`buildfs` triggers `tools/build_ui.py`, which runs `npm run build` in `tiltbridge_web_ui/` and
copies the Vite output into `data/` before the LittleFS image is made. This is the only target that
touches the UI, which is why a plain `pio run` does not need Node installed.

`data/wifiui/` is preserved across UI rebuilds — it belongs to `esp_wifi_config`, not to the Vue app.

So a full flash after changing both firmware and UI is:

```bash
~/.platformio/penv/bin/pio run -e esp32_headless --target upload --target buildfs --target uploadfs
```

### Do not combine `upload` with `buildfs`/`uploadfs`

```bash
# BROKEN - writes the filesystem twice and never writes the firmware
pio run -e esp32_headless --target upload --target buildfs --target uploadfs
```

PlatformIO reorders and dedupes targets, and the firmware write silently disappears: the log
shows two writes to `0x00330000` (the LittleFS partition) and none to `0x00010000` (the app).
The device reboots running the OLD firmware with the NEW web UI, which looks like the firmware
change simply did not work.

**Always run them as separate commands**, and check the addresses in the output:

```bash
pio run -e esp32_headless --target upload                      # expect 0x00010000
pio run -e esp32_headless --target buildfs --target uploadfs   # expect 0x00330000
```

### If upload fails

Uploads fail transiently often enough to be worth knowing: the log can show `Hash of data
verified` and still end in `[FAILED]`. **Just run it again** — a retry has always worked.

A failed firmware write can leave a bad app image, and then the device never joins WiFi. That
looks alarming but is not a brick: esptool still talks to it over serial. Confirm and re-flash:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port /dev/cu.usbserial-0001 --no-stub flash_id
pio run -e esp32_headless --target upload --upload-port /dev/cu.usbserial-0001
```

Most boards auto-reset into the bootloader via DTR/RTS. If yours does not, hold **BOOT/GPIO0**,
tap **RESET**, release BOOT, then start the upload. `upload_speed` is 460800; drop it in
`platformio.ini` if you see checksum errors on a long or unpowered USB cable.

---

## 5.1 Flashing the filesystem DESTROYS the device configuration

`uploadfs` writes a whole LittleFS image, and that partition holds more than the web UI:

| Path | Contents | Survives `uploadfs`? |
|---|---|---|
| `/littlefs/conf/tiltbridgeConfig.json` | all settings, target URLs, calibration | **no** |
| `/littlefs/conf/devices.json` | per-Tilt names, sheet names, per-device calibration | **no** |
| `/littlefs/queue/` | readings not yet uploaded | **no** |
| NVS partition | WiFi credentials, queue counters | yes |

WiFi survives, so the device comes back on the network — with a default configuration.

**Back up and restore around every `uploadfs`:**

```bash
# 1. Wait until nothing is queued, or those readings are lost for good
curl -s http://<device>/api/queue/     # queuedReadings must be 0

# 2. Back up
curl -s http://<device>/api/settings/json/ -o config.json
curl -s http://<device>/api/devices/     -o devices.json

# 3. Flash, then restore (see the ordering warning below)
```

Restore is plain unauthenticated PUTs — there is no CSRF check in the firmware:

| Endpoint | Carries |
|---|---|
| `PUT /api/devices/` | one body per Tilt: `deviceId`, `colorIndex`, `friendlyName`, `googleSheetsName`, … |
| `PUT /api/settings/targets/` | `scriptsURL`, `scriptsEmail`, `gsheetsV2Enabled`, `gsheetsPushEvery`, … |
| `PUT /api/settings/calibration/` | `applyCalibration`, `tempCorrect` |
| `PUT /api/settings/controller/` | `mdnsID`, `tzOffset`, `tempUnit`, queue settings, … |

**Restore in that order.** Device configs and sheet names must land *before* anything that can
trigger a reading capture — writing `queueSnapshotIntervalSec` triggers one immediately, and a
Tilt with no configuration yet produces an empty sheet name, which makes the Apps Script fall
back to the colour and create stray `Red`/`Green`/`Black` tabs.

Note `TZoffset` is **served** under that name but was historically only **accepted** as
`tzOffset`. Both spellings are accepted now, but a backup taken from an older build and PUT back
verbatim will silently drop the timezone.

---

## 6. Serial monitor

```bash
~/.platformio/penv/bin/pio device monitor -e esp32_headless
```

Using `-e` matters: it picks up `monitor_speed = 115200` and, more importantly,
`monitor_filters = esp32_exception_decoder`, which turns a panic backtrace into file-and-line
instead of raw addresses. Without the environment you get 115200 defaults and unreadable crashes.

`Ctrl-C` exits. The monitor holds the port open, so **stop it before flashing**.

### Capturing the log non-interactively

`pio device monitor` needs a real TTY and dies with `termios.error: (19, 'Operation not
supported by device')` if its output is piped or redirected — so it cannot be used from a
script. Use pyserial directly (it ships in the PlatformIO venv):

```python
import serial, time, sys
s = serial.Serial("/dev/cu.usbserial-0001", 115200, timeout=0.2)

# Classic ESP32 auto-reset: EN low via DTR/RTS, then release into normal run mode.
s.setDTR(False); s.setRTS(True); time.sleep(0.15)
s.setRTS(False);                 time.sleep(0.05)
s.reset_input_buffer()

deadline, out = time.time() + 12, bytearray()
while time.time() < deadline:
    out += s.read(4096)
s.close()
sys.stdout.write(out.decode("utf-8", "replace"))
```

Omit the DTR/RTS lines to attach without resetting a running device. The boot log is the fastest
way to identify a board: it prints the partition table, `Project name`, `App version`, the
compile time, and which subsystems came up.

Log verbosity is compile-time, in `[common] build_flags`: `ARDUINO_LOG_LEVEL` (6 = most verbose),
`CORE_DEBUG_LEVEL`, `CONFIG_NIMBLE_CPP_LOG_LEVEL` and `PRINT_GRAV_UPDATES`.

---

## 7. Testing the parts that are not firmware

### Web UI

```bash
cd tiltbridge_web_ui
npm install          # first time only
npm run build        # what buildfs runs; catches template and import errors
npm run dev          # hot-reload dev server against a live device's API
npm run lint
```

`npm run build` is a genuine check — a bad `v-model`, a missing import or a broken i18n key fails
it. Run it after editing anything under `tiltbridge_web_ui/src/`.

`dist/` and `data/` are both gitignored: they are build artifacts, never committed.

### Google Apps Script

`GoogleSheets/post_tilt.gs` runs on Google's servers, so it cannot be exercised by a firmware
build. Two things you can do locally:

```bash
# Syntax check (it is plain V8 JavaScript)
cp GoogleSheets/post_tilt.gs /tmp/post_tilt.js && node --check /tmp/post_tilt.js
```

For behaviour, stub the Apps Script globals (`SpreadsheetApp`, `PropertiesService`, `LockService`,
`Utilities.formatDate`) and run the file under Node's `vm` module against a fake sheet — a grid of
values, backgrounds, notes and font weights with `getRange`/`getValues`/`setValues` on it. That is
how the derived-column rebuild described in `docs/phase1/APPS_SCRIPT_PROTOCOL.md` §5.2 was
verified.

On the device side, the endpoint can be exercised by hand:

```bash
curl -L -X POST "$APPS_SCRIPT_URL" -H 'Content-Type: application/json' -d '{
  "schemaVersion": 2, "deviceName": "test",
  "readings": [{"recordId":"t1","Beer":"Test Wine","Temp":68.0,"SG":1.0900,
                "CapturedAtUtc":"2026-08-11T10:00:00Z","TimestampValid":true}]
}'
```

`-L` is required: Apps Script answers a web-app POST with a redirect to `googleusercontent.com`.
A correct response echoes the id back in `acceptedRecordIds` — that acknowledgement is what makes
the device drop the record from its offline queue.

---

## 8. Checking a running device over HTTP

No credentials are needed. `mdnsID` is configurable, so prefer the IP once you know it.

| Endpoint | Answers |
|---|---|
| `/api/version/` | firmware version, **git branch and commit**, `hardware` string — the reliable way to confirm a flash landed and which board build is running |
| `/api/queue/` | `queuedReadings`, `bytesUsed`, `uploadStatus`, `lastUploadSuccessAgeSec`, `maxRecordsSupported`, `activeTilts`, `estimatedRunwayHours` |
| `/api/errors/` | per-target `error_code` (0 = OK) and last attempt time |
| `/api/json/` | live Tilt readings, each with `mac`, `hasDeviceConfig`, `gsheets_name` |
| `/api/settings/json/` | the whole configuration — this is the backup |
| `/api/devices/` | per-Tilt configurations |
| `/api/uptime/`, `/api/heap/` | uptime, free heap and fragmentation |

`hardware` tells you which environment the running firmware was built from (`Headless`,
`Small TFT`, `S3 OLED`, …), which is how to pick the right `-e` without guessing.

### Recipes

**Confirm a flash landed** — `/api/version/` reports `branch` and `build` from `tools/git_rev.py`,
so it changes when you flash a different commit.

**Wait for the queue to drain before flashing the filesystem:**

```bash
until [ "$(curl -s http://<device>/api/queue/ | python3 -c \
  'import json,sys;print(json.load(sys.stdin)["queuedReadings"])')" = "0" ]; do sleep 20; done
```

**Simulate an outage** without unplugging anything — point the script URL at a deployment that
does not exist, watch the queue grow at the persistence interval, then restore the real URL and
watch it drain:

```bash
curl -X PUT http://<device>/api/settings/targets/ -H 'Content-Type: application/json' -d '{
  "scriptsURL":"https://script.google.com/macros/s/INVALID_FOR_TEST/exec",
  "scriptsEmail":"you@example.com","gsheetsV2Enabled":true,"gsheetsPushEvery":600}'
```

Records only reach flash while sending is failing, so `bytesUsed` staying at 0 during normal
operation is the check that the live path is working.

---

## 9. Useful extras

```bash
# Everything PlatformIO knows about an environment
~/.platformio/penv/bin/pio project config

# Wipe all build output
~/.platformio/penv/bin/pio run --target clean

# Erase the whole flash chip, config and queue included — a true factory reset
~/.platformio/penv/bin/pio run -e esp32_headless --target erase
```

`--target erase` destroys the saved configuration *and* any readings still sitting in the offline
queue. Drain the queue first if those readings matter.

Version and branch strings come from `tools/git_rev.py` at build time and land in `version.cpp`,
which is why the About page knows your git branch.

---

## 10. Editing config settings

Adding a setting means touching five places, and missing one fails quietly:

1. `src/jsonconfig.h` — the field on `Config`, with its default
2. `src/JsonKeys.h` — the JSON key constant
3. `src/jsonconfig.cpp` — `to_json()` to persist it, `load_from_json()` to read it back with bounds
4. `src/http_server.cpp` — the matching `process*Settings()` handler, and the dispatcher in
   `processTargetSettings()` if a new key alone must be routable
5. `tiltbridge_web_ui/src/stores/ConfigStore.js` plus the relevant `.vue` panel

Clamp on load as well as in the HTTP handler. The handler protects against a bad UI; the loader
protects against a hand-edited config file, and only the loader runs at boot.

Watch the size: `serializeConfig()` refuses to write a config that measures over
`JSON_CONFIG_BUFFER_SIZE` (8192 bytes) and **returns false rather than raising anything visible**,
so an oversized config silently stops saving.
