# Stage 6: MAC/device-specific configuration with color fallback

Spec §2, §3, §21.

## Storage decision: a separate file, not the main config

`Config` serialises through a single 8192-byte buffer (`JSON_CONFIG_BUFFER_SIZE`,
`jsonconfig.cpp:20`) and `serializeConfig()` **fails outright** if the document exceeds it
(`jsonconfig.cpp:137`). The existing config is already roughly 3–4 KB. A device table of 12
entries at ~230 B each would risk silently breaking config saves for users with many targets
configured.

So: device configuration lives in its own file.

```
/littlefs/conf/devices.json
```

Independently loaded and saved, independently size-capped, and a corrupt/absent file degrades
exactly to today's color-only behaviour — which is the §3 requirement.

## New files

`src/device_config.h`, `src/device_config.cpp`

## On-disk format

```json
{
  "schemaVersion": 1,
  "devices": [
    {
      "deviceId": "88:C2:55:AC:26:81",
      "mac": "88:C2:55:AC:26:81",
      "color": "Red",
      "colorIndex": 0,
      "friendlyName": "Cabernet Tank 1",
      "googleSheetsName": "Cabernet Tank 1",
      "modelLabel": "Pro Family",
      "notes": "",
      "enabled": true,
      "cal": { "x0": 0.0, "x1": 1.0, "x2": 0.0, "x3": 0.0 },
      "aliases": ["AA:BB:CC:DD:EE:FF"]
    }
  ]
}
```

`deviceId` and `mac` carry the same value (spec §2 shows both; keep both for payload
compatibility). `deviceId` is the key. `aliases` covers §21's "optional repeater aliases": a
repeater rebroadcasting the same physical Tilt under a different MAC maps onto this entry.

## In-memory structures

```cpp
// src/device_config.h
#define MAX_DEVICE_CONFIGS 12          // 12 * ~240B ≈ 2.9 KB JSON, well inside its own 4 KB file
#define MAX_DEVICE_ALIASES 2
#define DEVICE_ID_LEN 18               // "88:C2:55:AC:26:81" + NUL

struct DeviceConfig {
    char        deviceId[DEVICE_ID_LEN] = "";
    uint8_t     colorIndex = TILT_NONE;        // resolved from the advert, stored for display
    char        friendlyName[33] = "";
    char        googleSheetsName[26] = "";     // same 25-char cap as GsheetsConfig::name
    char        modelLabel[17] = "";           // manual label, §21
    char        notes[65] = "";
    bool        enabled = true;
    bool        hasCalibration = false;        // false => fall back to color calibration
    TiltCalData cal;                           // reuse the existing struct from jsonconfig.h
    char        aliases[MAX_DEVICE_ALIASES][DEVICE_ID_LEN] = {};
    char        gsheetsLink[256] = "";         // doclongurl cache, per device (mirrors GsheetsConfig::link)

    bool isSet() const { return deviceId[0] != '\0'; }
};

class DeviceConfigStore {
public:
    bool load();                       // absent/corrupt file => empty table, returns true
    bool save();

    // --- lookup, §3 priority: device-specific first, color fallback second ---
    DeviceConfig*       find(const char *deviceId);              // exact or alias match
    const DeviceConfig* find(const char *deviceId) const;
    DeviceConfig*       findOrCreate(const char *deviceId, uint8_t colorIndex);
    bool                remove(const char *deviceId);

    // Resolved accessors — these are what the senders call. Each falls back to the
    // existing color-keyed config when no device entry exists.
    const char* sheetName(const char *deviceId, uint8_t colorIndex) const;
    const char* displayName(const char *deviceId, uint8_t colorIndex) const;
    TiltCalData calibration(const char *deviceId, uint8_t colorIndex) const;
    bool        isEnabled(const char *deviceId) const;           // true when no entry exists
    const char* modelLabel(const char *deviceId, uint8_t colorIndex) const;

    size_t count() const;
    void   to_json(JsonDocument &doc) const;
    bool   from_json(const JsonDocument &doc);      // used by the PUT handler

    DeviceConfig devices[MAX_DEVICE_CONFIGS];
};

extern DeviceConfigStore device_config;

// Canonical form: uppercase, colon-separated. NimBLEAddress::toString() returns
// lowercase colon-separated — normalise once at the boundary so lookups are exact.
void canonicalizeDeviceId(const char *in, char *out, size_t outSize);
```

`canonicalizeDeviceId` matters: `NimBLEAddress::toString()` yields `88:c2:55:ac:26:81` while the
spec's examples and the UI table show uppercase. Normalise to **uppercase** everywhere; do the
conversion in one place and use it in the scanner, the config store, the queue records, and the
HTTP handlers so nothing compares mixed case.

## Fallback rules (§3)

| Accessor | Device entry present and field non-empty | Otherwise |
|---|---|---|
| `sheetName` | `googleSheetsName` | `config.gsheets_config[colorIndex].name` |
| `displayName` | `friendlyName` | `tilt_color_names[colorIndex]` |
| `calibration` | `cal` when `hasCalibration` | `config.tilt_calibration[colorIndex]` |
| `isEnabled` | `enabled` | `true` |
| `modelLabel` | `modelLabel` | derived: `tilt_pro ? "Pro Family" : "Standard"` |

Grainfather stays color-only this phase — §21 does not list it and §29 says keep the phase focused.

## Touch points

### `src/tilt/tiltHydrometer.{h,cpp}`

1. Cache a canonical id on the object so nothing re-formats it per send:
   ```cpp
   char m_device_id[DEVICE_ID_LEN];      // set in the ctor and whenever m_address changes
   const char* deviceId() const { return m_device_id; }
   ```
   `tiltScanner.cpp:157` currently assigns `th->m_address = lookup_address;` *after*
   `set_values()` — move/add a `th->setAddress(lookup_address)` that also refreshes
   `m_device_id`.

2. `apply_calibration()` (`tiltHydrometer.cpp:97`) — replace the four
   `config.tilt_calibration[m_color].xN` reads with
   ```cpp
   const TiltCalData cal = device_config.calibration(m_device_id, m_color);
   ```
   This is the one change that touches the gravity path. §6 forbids changing the *algorithm* —
   the polynomial and the smoothing are untouched, only the coefficient source changes, and with
   no device entry the coefficients are bit-identical to today.

3. `to_json()` (`tiltHydrometer.cpp:272`) — add for the UI (§20):
   ```cpp
   j["deviceId"]     = m_device_id;
   j["mac"]          = m_device_id;              // now canonical uppercase; was m_address.toString()
   j["friendlyName"] = device_config.displayName(m_device_id, m_color);
   j["modelLabel"]   = device_config.modelLabel(m_device_id, m_color);
   j["enabled"]      = device_config.isEnabled(m_device_id);
   j["hasDeviceConfig"] = device_config.find(m_device_id) != nullptr;
   ```
   Keep `gsheets_name`/`gsheets_link` but resolve them through the store so the UI shows the
   effective value:
   ```cpp
   j["gsheets_name"] = device_config.sheetName(m_device_id, m_color);
   ```
   **`TILT_DATA_SIZE` (477) / `TILT_ALL_DATA_SIZE`** in `tiltHydrometer.h:9` are used to size
   `char tilt_data[TILT_ALL_DATA_SIZE + 128]` in `legacy_fermentrack.cpp:23`. Adding keys to
   `to_json()` grows the legacy payload. Legacy FT uses `to_json(true)`, so **gate the new keys
   behind `if (!legacy_keys)`** and leave the legacy branch's size unchanged. Safer than
   re-tuning the constant.

### `src/tilt/tiltScanner.cpp`

- After `th->set_values(...)`, skip nothing — always record the advert (RSSI stats need every
  advert, stage 7). Enforcement of `enabled` happens at *send* time, not scan time, so a disabled
  Tilt still shows on the Detected Devices page (§20 shows every physical device).
- `combineTilts` interaction: when `combineTilts` is on, `lookup_address` is the *bridge's* MAC,
  so all same-color Tilts share one `deviceId` and per-device config is meaningless. Keep the
  feature (backward compatibility) but have the UI disable per-device config editing while
  `combineTilts` is true, and note it in the panel. Do not auto-change the user's setting.

### Senders — resolve names through the store

- `sendData.cpp:500` `if (strlen(config.gsheets_config[th.m_color].name) > 0)` →
  `const char *sheet = device_config.sheetName(th.deviceId(), th.m_color); if (sheet && *sheet)`
- `sendData.cpp:507` `payload["Beer"] = sheet;`
- `sendData.cpp:539-542` doclongurl cache → write to the device entry when one exists, else the
  color entry (keep both working).
- Add an `isEnabled` skip to the per-tilt loops in the senders that produce one row per Tilt:
  `sendData.cpp:282` (BF/BF), `:327` (Grainfather), `:390` (taplist), `:447` (brewstatus),
  `:498` (Google), `:594` (InfluxDB), and `tiltScanner::tilt_to_json_legacy()`. One guard each:
  ```cpp
  if (!device_config.isEnabled(th.deviceId())) continue;
  ```
  Defaults to `true`, so existing installs send exactly what they send today.
- Where a payload currently sends `tilt_color_names[th.m_color]` as the *name*
  (`sendData.cpp:287` `j["name"]`), switch to `device_config.displayName(...)` — which returns the
  color name unless the user set a friendly name. `Color` fields stay the raw color
  (`sendData.cpp:396,512`) so downstream integrations do not break.

### `src/http_calibration.cpp` — per-device calibration points

Point files are `"%s/%d-cal.json"` keyed by color (`:135,237,262,283`). Extend rather than
replace:

```cpp
// color-only (unchanged, still used when no deviceId is supplied)
/littlefs/conf/0-cal.json
// device-specific
/littlefs/conf/dev-88C255AC2681-cal.json      // colons stripped: LittleFS-safe, <32 chars
```

Add an optional `"deviceId"` field to the three calibration endpoints' payloads
(`processCalibrationDataPoint`, `processCalibrationCoefficients`, `processCalibrationDataDelete`).
When present, operate on the device file and on `device_config.devices[i].cal`
(setting `hasCalibration = true`); when absent, keep today's exact behaviour. Factor the filename
choice into one helper:

```cpp
static void calibrationFilename(const char *deviceId, uint8_t color, char *out, size_t outSize);
```

`getCalibrationPoints()` gains the same optional parameter. Its caller is the calibration GET
endpoint — check `http_calibration.h` and `CalibrationStore.js` for the query-string shape before
changing the signature.

### `src/main.cpp`

`device_config.load();` immediately after `config.load();` in `setup()`.

## New HTTP API

| Method | Path | Body / behaviour |
|---|---|---|
| GET | `/api/devices/` | `{ "schemaVersion":1, "devices":[…], "maxDevices":12 }` — the stored table |
| PUT | `/api/devices/` | Upsert one device: `{"deviceId":"88:C2:55:AC:26:81", "friendlyName":…, "googleSheetsName":…, "enabled":true, "modelLabel":…, "notes":…, "aliases":[…]}`. Creates the entry if absent (§3: "provide a way for users to create an individual configuration for a detected physical Tilt"). |
| POST | `/api/devices/delete/` | `{"deviceId":"…"}` — drops the entry; that device reverts to color config |

Register in `httpServer::registerJsonGetHandlers()` / `registerJsonPutHandlers()` following the
existing `MAKE_GET_HANDLER` / `MAKE_PUT_HANDLER` pattern (`http_server.cpp:668-696`). Note the
existing PUT registration loop only registers `HTTP_PUT`; the delete endpoint needs `HTTP_POST`
like the calibration delete (`http_server.cpp:799`).

Validation in the PUT handler:
- `deviceId` must be 17 chars matching `^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$` → canonicalize.
- Reject when the table is full (`MAX_DEVICE_CONFIGS`) with a 400 and a clear message.
- Trim/clamp strings with `strlcpy` to the struct sizes, same as `updateJsonSetting`.
- `device_config.save()` after a successful mutation; return `idf_json_send_status`.

## Backward-compatibility checks before this stage is done

- [ ] Fresh flash with an existing beta5 `tiltbridgeConfig.json`, no `devices.json`: Google Sheets
      names, calibration, and Grainfather URLs all behave identically.
- [ ] `devices.json` deliberately corrupted: device features vanish, nothing else breaks, a
      warning is logged, the file is replaced on next save.
- [ ] Two same-color Tilts with different MACs and no device entries: both send under the color
      name (today's behaviour, duplicate rows) — the fix is opt-in, not forced.
- [ ] `combineTilts = true` still merges as before.

**Build here.**
