# Stage 9: Queue settings

Spec §8, §10, §22. Small stage — config fields, validation, HTTP plumbing. UI in `09-web-ui.md`.

## Config additions (`src/jsonconfig.h`, class `Config`)

```cpp
// Offline queue (§8, §10, §22) — deliberately separate from every cloud push interval.
uint16_t queueSnapshotIntervalSec = 1800;   // 30 minutes, §8 default
uint16_t maxQueuedRecords         = 1500;   // §10 default, ~7 days at 4 Tilts / 30 min
bool     offlineQueueEnabled      = true;
uint8_t  queueBatchSize           = 20;     // §14 default

// Enhanced Google Sheets mode (§13) — legacy path stays the default so existing
// installs are untouched until the user opts in.
bool     gsheetsV2Enabled         = false;
```

## Serialisation

`Config::to_json()` (`jsonconfig.cpp:251`) — add alongside the other general settings:

```cpp
obj[QueueSettings::queueSnapshotIntervalSec] = queueSnapshotIntervalSec;
obj[QueueSettings::maxQueuedRecords]         = maxQueuedRecords;
obj[QueueSettings::offlineQueueEnabled]      = offlineQueueEnabled;
obj[QueueSettings::queueBatchSize]           = queueBatchSize;
obj[GoogleSheetsSettings::gsheetsV2Enabled]  = gsheetsV2Enabled;
obj["senderRecoveryEnabled"]                 = senderRecoveryEnabled;   // from stage 5
obj["senderStaleRebootSec"]                  = senderStaleRebootSec;
```

`Config::load_from_json()` (`jsonconfig.cpp:315`) — the existing `if (!obj[key].isNull())` idiom
means old config files simply keep the defaults above (§3). Clamp on load, because a hand-edited
file must not be able to brick the device:

```cpp
if (!obj[QueueSettings::queueSnapshotIntervalSec].isNull()) {
    queueSnapshotIntervalSec = int(obj[QueueSettings::queueSnapshotIntervalSec]);
    if (queueSnapshotIntervalSec < 60 || queueSnapshotIntervalSec > 21600)   // 1 min .. 6 h
        queueSnapshotIntervalSec = 1800;
}
if (!obj[QueueSettings::maxQueuedRecords].isNull()) {
    maxQueuedRecords = int(obj[QueueSettings::maxQueuedRecords]);
    if (maxQueuedRecords < 100 || maxQueuedRecords > 3000)                   // 3000 * 128 B = 384 KB
        maxQueuedRecords = 1500;
}
if (!obj[QueueSettings::queueBatchSize].isNull()) {
    queueBatchSize = int(obj[QueueSettings::queueBatchSize]);
    if (queueBatchSize < 1 || queueBatchSize > 50) queueBatchSize = 20;
}
```

The lower bound of 60 s on the snapshot interval exists to protect flash (§25). The UI offers
10/15/30/60 min presets **plus** a free numeric field (§8: "do not hard-code only these choices if
numeric configuration is already easy to support" — it is, `updateJsonSetting(uint16_t&)` already
exists at `http_server.cpp:184`).

### The snapshot interval is not an upload interval

These are three different settings and it is worth being explicit, because "how often does my
TiltBridge send data" has three plausible answers depending on which one you mean:

| Setting | Where it lives | What it controls |
|---|---|---|
| `queueSnapshotIntervalSec` | Queue page (device-wide) | how often a reading is **captured** — the sampling rate, and therefore the row cadence in the spreadsheet |
| `<target>PushEvery` | each target's panel | how often captured readings are **uploaded** to that target — latency and batching only |
| `maxQueuedRecords` | Queue page (device-wide) | how long an **outage** the queue can absorb |

**The snapshot interval is not just a flash-wear knob, and the name undersells it.**
`take_queue_snapshot()` walks every enabled Tilt, builds a `QueuedReading` and appends it to
flash. The append is what persists the queue, which is where the name comes from — but the walk
is what *creates the reading*. Raising this interval does not merely write to flash less often;
it samples less often, and the spreadsheet gets proportionally fewer rows.

So the row cadence you see in the sheet is the **snapshot** interval. The push interval only
decides how long a captured row waits before it is uploaded, and it can never produce a row that
was never captured. A device on a 15-minute snapshot and a 10-minute push writes a row every
15 minutes, each arriving within 10 minutes of capture.

Changing the snapshot interval **captures a reading immediately** and restarts the cadence from
that moment. `http_server` sets `queue_timer_restart_rqd`, the main loop turns that into a due
snapshot, and `take_queue_snapshot()` re-arms its own one-shot on the new interval.

It used to re-arm for a full interval instead, which meant a change always *postponed* the next
reading — so shortening the interval made the queue go quiet for longer than the old setting did.
That is the opposite of the intent and is indistinguishable from a broken queue, which is exactly
how it was found.

Every target now has its own `PushEvery`. Google Sheets, Fermentrack 2, Brewer's Friend,
Brewfather, Grainfather and the generic JSON target used to hold theirs as `#define`s in
`sendData.h`. Bounds are `PUSH_EVERY_MIN_SEC` (**10 minutes**) .. `PUSH_EVERY_MAX_SEC` (12 h),
shared by the loader and the HTTP handler so the UI can never accept a value the loader then
discards.

The 10-minute floor is policy, not a technical limit. A Tilt's gravity moves far too slowly for a
faster cadence to carry information, and each of these targets is a remote HTTP endpoint where
going faster costs battery, heap and — for Google Sheets, metered against a daily Apps Script
execution-time quota — the ability to upload at all later in the day.

Five of the six old `#define` values were already at or above the floor and carry over unchanged
(Google Sheets 600 s, Brewer's Friend / Brewfather / Grainfather 900 s, generic target 600 s), so
those devices behave exactly as before. **Fermentrack 2 is the exception**: `FERMENTRACK_DELAY`
was 5 minutes, below the floor, so its default moves to 600 s and existing installs will upload
half as often after this change.

The floor applies only to the six targets converted from compile-time constants. The intervals
that were already configurable — MQTT, Brewstatus, Taplist.io, InfluxDB and legacy Fermentrack —
keep their existing bounds, because those are commonly pointed at a broker or a server on the
local network where a 30-second cadence is free and useful.

Note that on the v2 path the push interval is the *idle* cadence. `send_to_google_v2()` reschedules
itself immediately while a backlog is draining, so a queue that built up during an outage still
empties as fast as the endpoint allows, whatever the interval is set to.

`queueBatchSize` upper bound of 50: a 50-record batch serialises to roughly 50 × ~260 B ≈ 13 KB of
JSON, which is already a large POST body for an ESP32 heap. 20 stays the default (§14).

## `src/JsonKeys.h`

```cpp
namespace QueueSettings {
constexpr auto queueSnapshotIntervalSec = "queueSnapshotIntervalSec";
constexpr auto maxQueuedRecords         = "maxQueuedRecords";
constexpr auto offlineQueueEnabled      = "offlineQueueEnabled";
constexpr auto queueBatchSize           = "queueBatchSize";
}
```
and add `constexpr auto gsheetsV2Enabled = "gsheetsV2Enabled";` to the existing
`GoogleSheetsSettings` namespace.

## HTTP handler

Queue settings are device-wide, not per-target, so they belong with the controller settings rather
than the target dispatcher — `processTargetSettings()` (`http_server.cpp:591`) dispatches by
sniffing which target's keys are present, and adding queue keys there would be fragile.

Extend `processTiltBridgeSettingsJson()` (`http_server.cpp:195`), which already handles `mdnsID`,
`tzOffset`, `tempUnit`, `smoothFactor`, `invertTFT`, `combineTilts`:

```cpp
if (json[QueueSettings::queueSnapshotIntervalSec].is<uint16_t>()) {
    uint16_t v = json[QueueSettings::queueSnapshotIntervalSec].as<uint16_t>();
    if (v < 60 || v > 21600) {
        Log.warning("Settings update error, [queueSnapshotIntervalSec]:(%d) not valid.\r\n", v);
        failCount++;
    } else {
        bool changed = (config.queueSnapshotIntervalSec != v);
        config.queueSnapshotIntervalSec = v;
        if (changed) http_server.queue_timer_restart_rqd = true;   // re-arm on the main loop
        Log.notice("Settings update, [queueSnapshotIntervalSec]:(%d) applied.\r\n", v);
    }
}
// same shape for maxQueuedRecords, queueBatchSize
updateJsonSettingBool(json, QueueSettings::offlineQueueEnabled, config.offlineQueueEnabled);
updateJsonSettingBool(json, "senderRecoveryEnabled", config.senderRecoveryEnabled);
```

Careful with `updateJsonSettingBool` (`http_server.cpp:157`): it returns `false` when the key is
**absent**, and the existing callers in `processCalibrationSettings` treat that as a failure
(`http_server.cpp:299-303`). Since queue keys are optional in a controller-settings PUT, do not
increment `failCount` on its return value here — just call it and ignore the result, the way
`combineTilts` is handled inline.

Add `bool queue_timer_restart_rqd = false;` to `httpServer` (`src/http_server.h:29`, next to the
other request flags) and handle it in `loop()` (`main.cpp`, alongside `mqtt_init_rqd`):

```cpp
if (http_server.queue_timer_restart_rqd) {
    http_server.queue_timer_restart_rqd = false;
    data_sender.startTimer(data_sender.queueSnapshotTimer, config.queueSnapshotIntervalSec);
}
```

`gsheetsV2Enabled` belongs in `processGoogleSheetsSettings()` (`http_server.cpp:372`) with the
other Sheets keys.

## Timer creation

`dataSendHandler::createTimers()` (`sendData.cpp:94`) — add:
```cpp
queueSnapshotTimer = xTimerCreate("QueueSnap", pdMS_TO_TICKS(1000), pdFALSE, nullptr, queueSnapshotTimerCallback);
```
`dataSendHandler::init()` (`sendData.cpp:119`) — first snapshot shortly after boot so the user sees
the queue working without waiting 30 minutes, then the configured interval takes over:
```cpp
startTimer(queueSnapshotTimer, 120);
```
Callback sets `data_sender.snapshot_due = true` (following the existing one-shot-flag pattern).

The reschedule at the end of `take_queue_snapshot()` uses `config.queueSnapshotIntervalSec`, so a
settings change takes effect at the next boundary even without the `queue_timer_restart_rqd` path;
the flag just makes it immediate.

## `reading_queue.init()` ordering in `main.cpp`

```cpp
filesystem_init(true);
config.load();
device_config.load();       // stage 6
reading_queue.init();       // needs config.maxQueuedRecords, so after config.load()
```

If `reading_queue.init()` fails, log an error and set `config.offlineQueueEnabled = false` **in
RAM only** (do not save) so the rest of the firmware runs normally on a broken filesystem.

**Build here.**
