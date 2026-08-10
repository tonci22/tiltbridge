# Stages 1–2: Inspection findings

Read-only stage. No code changes. Everything below is verified against the tree at `7a5d7e8`.

## 1. Current device identity and color configuration

### How a Tilt is identified today

`tiltScanner::load_tilt_from_advert_hex()` — `src/tilt/tiltScanner.cpp:98`

- Parses iBeacon manufacturer data; bytes 4–19 → UUID → `tiltHydrometer::uuid_to_color_no()` →
  color index 0–7 (`TILT_COLOR_RED` … `TILT_COLOR_PINK`).
- Chooses a lookup address at `:151`:
  ```cpp
  NimBLEAddress lookup_address = config.combineTilts
      ? NimBLEDevice::getAddress()          // TiltBridge's own MAC — merges all same-color units
      : advertisedDevice->getAddress();     // real per-device MAC
  ```
- `get_or_create_tilt(lookup_address, m_color)` keys the in-RAM list on **(address, color)**.

**So the scanner already tracks physical devices separately** (when `combineTilts` is off).
Multiple same-color Tilts already appear as separate `tiltHydrometer` objects in
`tilt_scanner.m_tilt_devices` (a `std::list<tiltHydrometer>`), and `to_json()` already emits
`j["mac"] = m_address.toString()` (`tiltHydrometer.cpp:300`).

### Where identity collapses to color

Everything downstream of the scanner indexes config by `th.m_color` only:

| Consumer | Location | Indexed by |
|---|---|---|
| Calibration coefficients | `tiltHydrometer::apply_calibration()` `tiltHydrometer.cpp:99` | `config.tilt_calibration[m_color]` |
| Google Sheets name + link | `sendData.cpp:500,507,539,541` | `config.gsheets_config[m_color]` |
| Grainfather URL | `sendData.cpp:329,347` | `config.grainfatherURL[m_color]` |
| UI hints in tilt JSON | `tiltHydrometer.cpp:305-306` | `config.gsheets_config[m_color]` |
| Calibration point files | `http_calibration.cpp:135` | `"%s/%d-cal.json", CONFIG_DIR, color` |
| Config persistence | `jsonconfig.cpp:266-276, 374-408` | `obj[tilt_color_names[x]]` |
| Config HTTP keys | `JsonKeys.h:1` `tiltColorSuffixes[]`, `http_server.cpp:383,460` | `"sheetName_red"` etc. |
| Legacy FT payload | `tiltScanner::tilt_to_json_legacy()` `tiltScanner.cpp:190` | dict keyed by color name |
| BF/BF, taplist, brewstatus, influx payloads | `sendData.cpp:287,396,452`, `sendData.cpp:608` | color name as the device name |

Consequence today: four Red Tilts share one calibration curve, one sheet name, one Grainfather
URL, and produce four rows that are indistinguishable downstream.

### Config storage shape

`Config` (`src/jsonconfig.h:54`) holds three fixed `[TILT_COLORS]` arrays:
`tilt_calibration`, `gsheets_config`, `grainfatherURL`. Serialized as
`obj["Red"]["x0"] … obj["Red"]["name"] … obj["Red"]["grainfatherURL"]`. Whole config is one
JSON file `/littlefs/conf/tiltbridgeConfig.json`, buffer cap `JSON_CONFIG_BUFFER_SIZE 8192`
(`jsonconfig.cpp:20`) — **watch this ceiling**, a MAC-keyed device table must not blow it.

`Config::load_from_json()` uses `if (!obj[key].isNull())` for every field, so **adding keys is
backward compatible** and missing keys keep struct defaults. Note `deserializeConfig()`
(`jsonconfig.cpp:115`) ignores parse errors and calls `load_from_json` either way — a corrupt
config silently yields all-defaults.

### Model / "Pro family" detection

`tilt_pro` is inferred in `set_values()` from `i_grav >= 5000` (`tiltHydrometer.cpp:123`).
`version_code` is captured when `i_temp == 999`. `receives_battery` is inferred from a 197
`tx_pwr` followed by a non-197. There is no stored model label — §21's "manual model label"
must be new config.

## 2. `send_lock` audit

Declared `bool send_lock = false;` — `src/sendData.h:134` (private to `dataSendHandler`).

All 34 references, by call site:

| Sender | File:line | Pattern | Leak-free on all returns? |
|---|---|---|---|
| Brewer's Friend | `sendData.cpp:159-177` | `if (flag && !lock) { lock=true; …; lock=false; }` | yes |
| Brewfather | `sendData.cpp:180-198` | same | yes |
| User target | `sendData.cpp:202-221` | same | yes |
| Grainfather | `sendData.cpp:316-353` | same | yes |
| taplist.io | `sendData.cpp:370-415` | early `return false` **before** `lock=true` (`:370-373`) | yes |
| Brewstatus | `sendData.cpp:426-469` | same as first pattern | yes |
| Google Sheets | `sendData.cpp:479-563` | same | yes |
| InfluxDB | `sendData.cpp:573-647` | same | yes |
| Legacy Fermentrack | `targets/legacy_fermentrack.cpp:13-52` | same | yes |
| Fermentrack 2 | `targets/fermentrack_2.cpp:106-148` | same | yes |
| MQTT | `targets/mqtt.cpp:155-178` | same | yes |

**Conclusion: no ordinary control-flow path leaks the lock.** Real risks that remain:

1. **Not a mutex.** `if (send_brewersFriend && !send_lock) { send_lock = true; … }` is a
   test-then-set with no atomicity. Today all eleven senders are driven from the single
   `loopTask` (`main.cpp:140` → `data_sender.process()`), so no interleaving happens *yet* —
   but nothing enforces that, and Phase 1 adds a monitor task and a queue drain path.
2. **No timeout / no visibility.** Nothing records when the lock was taken or which target holds
   it, so a wedged request is invisible from the UI. This is exactly what §17/§18 require.
3. **Non-obvious semantics.** `!send_lock` means "skip this target this pass", not "wait".
   The mutex replacement must preserve *skip*, i.e. `xSemaphoreTake(mutex, 0)`, or the loop
   will start blocking.
4. **A crash inside a send would leave it set** — and would also reboot, so this is not the
   observed freeze.

### Sender flow, end to end

```
FreeRTOS one-shot timers (sendData.cpp:50-92)  ──set──▶  send_<target> = true
                                                              │
loopTask (main.cpp:200) ──▶ loop() ──▶ data_sender.process() ─┤
                                          │                   │
                                    if (is_wifi_connected())   │  ← ★ single point of total failure
                                          │                   ▼
                                          └──▶ send_to_<target>()
                                                   ├─ claims send_lock
                                                   ├─ clears its own flag
                                                   ├─ tilt_scanner.drop_expired_tilts()
                                                   ├─ for each tilt: build payload, http_request()
                                                   ├─ setTargetStatus(target, err)
                                                   ├─ startTimer(<target>Timer, DELAY)  ← reschedules
                                                   └─ releases send_lock
```

★ `is_wifi_connected()` → `wifi_cfg_is_connected()` (`wifi_setup.cpp:255`). See
`00-OVERVIEW.md` — this is the prime suspect for the reported failure, because a stale-false
flag here silently disables **all** outbound work while leaving BLE and the web server intact.

Note the reschedule happens **inside** the guarded block. So if a target is skipped because the
lock is held, its `send_*` flag stays true and it retries next loop pass — good. But if
`process()` is never entered at all, the flags stay true forever and the one-shot timers have
already fired, so nothing re-arms them: **recovery requires a reboot.** Matches the report.

### Transport-level observations (`send_json_str.cpp`)

- `http_request()` honours `options.timeoutMs` (default 6000, Google 10000, InfluxDB 6000) via
  `esp_http_client_config_t::timeout_ms`. Note this is a per-socket-operation timeout, not a
  total-request deadline, so a slow-loris peer can still exceed it in aggregate.
- mDNS resolution happens first, in `buildResolvedUrl()` → `resolveHostToString()`
  (`url_utils.cpp`). This runs before any timeout is configured. Worth confirming it cannot
  block unbounded when checking targets that use `.local` hostnames (Fermentrack often does).
- Every exit path frees the client and the 2 KB heap response buffer. No leak found.

### Latent bug found while auditing (fix in stage 4)

`sendData.cpp:539`:

```cpp
if(strcmp(config.gsheets_config[th.m_color].link, retval["doclongurl"].as<const char *>()) != 0)
```

If the Apps Script response is not JSON, or lacks `doclongurl`, `as<const char*>()` yields
`nullptr` and `strcmp` dereferences it → `LoadProhibited` panic → reboot. This is reachable
whenever Google returns an HTML error page with a 200. Guard it.
