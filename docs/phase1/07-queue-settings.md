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
