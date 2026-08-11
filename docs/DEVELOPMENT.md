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

### If upload fails

Most boards auto-reset into the bootloader via DTR/RTS. If yours does not, hold **BOOT/GPIO0**,
tap **RESET**, release BOOT, then start the upload. `upload_speed` is 460800; drop it in
`platformio.ini` if you see checksum errors on a long or unpowered USB cable.

---

## 6. Serial monitor

```bash
~/.platformio/penv/bin/pio device monitor -e esp32_headless
```

Using `-e` matters: it picks up `monitor_speed = 115200` and, more importantly,
`monitor_filters = esp32_exception_decoder`, which turns a panic backtrace into file-and-line
instead of raw addresses. Without the environment you get 115200 defaults and unreadable crashes.

`Ctrl-C` exits. The monitor holds the port open, so **stop it before flashing**.

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

## 8. Useful extras

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

## 9. Editing config settings

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
