# Stages 10–11: Enhanced batch Google Sheets protocol, acknowledgement and retry

Spec §6, §13, §14, §15, §16.

## Hard requirement: the legacy path stays

`dataSendHandler::send_to_google()` (`sendData.cpp:475`) is **not modified** beyond the
null-`doclongurl` crash fix from stage 4 and the device-config name resolution from stage 6. The
v2 protocol is a separate function in a separate file, reached only when
`config.gsheetsV2Enabled` is true.

```cpp
// sendData.cpp, inside process(), where send_to_google() is called today
if (config.gsheetsV2Enabled)
    send_to_google_v2();      // src/targets/gsheets_v2.cpp
else
    send_to_google();         // untouched legacy path
```

Both share `config.scriptsURL` / `config.scriptsEmail`. A user switching modes does not
reconfigure anything; they flip one checkbox and must update their Apps Script (documented in
`APPS_SCRIPT_PROTOCOL.md`).

## New file: `src/targets/gsheets_v2.cpp`

```cpp
bool dataSendHandler::send_to_google_v2();     // declared in sendData.h
```

Driven by the same `send_gSheets` flag and `gSheetsTimer` as the legacy path, so the push cadence
(`GSCRIPTS_DELAY`, 10 min) is unchanged. **The snapshot interval and the push interval are
independent** (§8): snapshots land in the queue every 30 min, uploads attempt every 10 min and
send whatever is pending. That is the whole point of the design — a 10-min push cadence with
30-min flash writes.

### Flow

```
1. SenderLock lock(TARGET_GOOGLE_SHEETS, 15000);  if (!lock) return
2. bail out unless scriptsURL/scriptsEmail pass the existing length checks
   (GSCRIPTS_MIN_URL_LENGTH, GSCRIPTS_MIN_EMAIL_LENGTH)
3. batch = reading_queue.peekBatch(buf, config.queueBatchSize)     // §14
4. if batch is empty -> reschedule, return true
5. serialise the batch (below)
6. POST, 15 s timeout, skipCertValidation = true (Google redirects to script.googleusercontent.com)
7. parse the response and acknowledge (§15)
8. reschedule gSheetsTimer
   - full success and more records pending -> re-arm at 5 s to drain the backlog quickly
   - otherwise -> GSCRIPTS_DELAY
```

Step 8's fast re-arm is what makes a multi-hour backlog drain in minutes instead of days, while
still bounding request count (one request per 5 s, 20 records each = 240 records/min).

### Memory

`config.queueBatchSize` records × 128 B = 2.5 KB of `QueuedReading` buffer, plus a serialised
JSON body of roughly 20 × 300 B ≈ 6 KB, plus ArduinoJson overhead. `loopTask` has an 8192-byte
stack (`main.cpp:216`) — **allocate both the record buffer and the payload string on the heap**,
never on the stack. `send_json_str.cpp:199` already sets this precedent for the response buffer.

Check `printMem()` before and after a batch send during testing; if heap headroom is thin, lower
the default batch size rather than raising the stack.

## Request payload (§13)

```json
{
  "schemaVersion": 2,
  "deviceName": "TiltBridge-Winery",
  "Email": "user@gmail.com",
  "tzOffset": -5,
  "readings": [
    {
      "recordId": "A91F2C-6A72-00000452",
      "deviceId": "88:C2:55:AC:26:81",
      "mac": "88:C2:55:AC:26:81",
      "Beer": "Cabernet Tank 1",
      "Color": "Red",
      "Temp": 68.2,
      "SG": 1.0456,
      "SG_Raw": 1.0458,
      "SG_Smoothed": 1.0456,
      "RSSI": -58,
      "RSSI_Avg": -61,
      "RSSI_Min": -73,
      "RSSI_Max": -55,
      "RSSI_Samples": 48,
      "CapturedAtUtc": "2026-08-10T08:40:00Z",
      "TimestampValid": true,
      "Comment": ""
    }
  ]
}
```

Field notes:

- `deviceName` = `config.mdnsID`. `Email` and `tzOffset` are carried at the envelope level (the
  legacy payload repeats them per row, `sendData.cpp:514-515`); the Apps Script needs `Email` to
  locate the spreadsheet.
- `Beer` = `sheetName` snapshotted into the record at capture time, so a record delivered after the
  user renames a Tilt still lands in the sheet it was captured for.
- `Temp` is Fahrenheit, matching every existing payload (`sendData.cpp:508` passes
  `fahrenheit_only = true`). Emit as a number, not a string — the legacy path sends strings
  (`payload["Temp"] = temp;` where `temp` is `char[6]`); v2 is a new protocol so use proper JSON
  numbers and say so in the docs.
- `SG` is the **normal final TiltBridge value** (§6) — `cal_smooth_gravity`, i.e. exactly what the
  legacy path sends. `SG_Raw` (`latest_gravity`) and `SG_Smoothed` (`uncal_smooth_gravity`) are the
  §6 intermediates; all three already exist as members, so no smoothing change is involved.
  Serialise with 4 decimals for standard Tilts and 4 for Pro (the existing `grav_to_str` uses
  `%.4f` regardless — keep that, and note that Tilt Pro's extra digit of resolution is preserved
  because the divisor is 10000).
- `CapturedAtUtc` is omitted entirely when `TimestampValid` is false (§12: do not fabricate). Add
  `"UptimeMsAtCapture"` in that case so the server can at least order the rows.
- **No battery percentage or voltage** (§13). `weeks_on_battery` is a real reported value, but it
  is not in the §13 field list, so leave it out of v2 this phase.

## Response and acknowledgement (§15)

Expected success response:

```json
{ "status": "ok", "acceptedRecordIds": ["A91F2C-6A72-00000452", "A91F2C-1234-00000453"] }
```

### Client handling matrix — all six cases §15 lists

| Situation | Detection | Action |
|---|---|---|
| Full success | HTTP 2xx, valid JSON, every sent id present in `acceptedRecordIds` | acknowledge all, `lastGoogleSuccess = now`, `consecutiveSendFailures = 0`, fast re-arm if more pending |
| Partial acknowledgement | HTTP 2xx, valid JSON, subset present | acknowledge **only** the listed ids; the rest stay queued and go out in the next batch with the same ids |
| Successful POST but lost response | `http_request` returns failure/timeout after the server committed | acknowledge nothing; the same batch is retried with the same record ids; the server suppresses duplicates by id → at-least-once + server-side dedup (§15) |
| HTTP error (4xx/5xx) | `httpCode` outside 200–204 | acknowledge nothing, `setTargetStatus(TARGET_GOOGLE_SHEETS, httpCodeToSendError(code))`, `consecutiveSendFailures++`, normal `GSCRIPTS_DELAY` retry — **no reboot** (§19) |
| Timeout | `esp_http_client_perform` error, `httpCode = -1` | as HTTP error, `SEND_ERR_CONNECTION_FAILED` |
| Malformed response | `deserializeJson` error, missing `status`, or `acceptedRecordIds` not an array | acknowledge nothing, `SEND_ERR_OTHER`, log the first 256 bytes of the body, retry later |

Explicit rule from §15: **a record is deleted only on an id appearing in `acceptedRecordIds`.**
Never on "the POST returned 200". A 200 with an unparseable body acknowledges nothing.

### Response buffer sizing

A 20-record batch acknowledges 20 × 21 chars of id + separators ≈ 500 B, plus envelope. But Apps
Script commonly returns an HTML error page on failure, and `http_request`'s internal buffer is a
fixed 2048 B (`send_json_str.cpp:198`) that silently truncates beyond that. Pass a 2048-byte heap
response buffer and treat truncation as "malformed" — a truncated JSON array will fail to parse,
which lands in the right bucket. If batch size is ever raised above ~50, the internal
`RESPONSE_BUF_SIZE` needs raising too; note the coupling in a comment at both sites.

### Idempotency detail

`peekBatch()` returns the same records in the same order until they are acknowledged, and record
ids live on flash (§11), so a retry after a reboot mid-transmission sends byte-identical ids. That
is what makes server-side dedup sufficient.

## Fermentrack isolation (§16)

§16: "a stuck Fermentrack request must not permanently prevent Google Sheets or every other
outbound target from operating."

Today all eleven senders share one lock and run sequentially in `process()`, so a wedged
Fermentrack request blocks everything for the duration of its timeout. Phase 1 does the minimum:

1. **Bounded blocking.** Fermentrack 2 makes three sequential requests
   (`fermentrack_2.cpp:114-116`); its `SenderLock` timeout budget is `3 × 6000 = 18000` ms
   (stage 4), and the stale detector flags anything past 3× that (stage 5).
2. **Circuit breaker.** When a target's `consecutiveSendFailures` exceeds 5, back its retry
   interval off exponentially (2×, capped at 30 min) instead of retrying on its normal schedule.
   This stops a dead Fermentrack from consuming a lock slot every 5 minutes.
   Store the multiplier in RAM on `SendTargetStatus`; reset on the first success.
3. **Ordering.** Move `send_to_google()` / `send_to_google_v2()` **before** the two Fermentrack
   calls in `process()` (`sendData.cpp:143-151`) so that within a single pass Sheets gets the lock
   first. Cheap, and it directly serves §16's wording.

Persistent Fermentrack queuing is explicitly **not** in this phase (§16, §29).

## `sendData.h` additions

```cpp
bool send_to_google_v2();
void take_queue_snapshot();
TimerHandle_t queueSnapshotTimer;
bool snapshot_due = false;
bool send_backlog_now = false;      // set by /api/queue/actions/
uint32_t lastQueueUploadSuccessMs = 0;
enum class QueueUploadState : uint8_t { IDLE, SENDING, RETRYING, DISABLED } queueUploadState = QueueUploadState::IDLE;
```

`queueUploadState` feeds `/api/queue/`'s `uploadStatus` (§23).

## Documentation deliverable

`APPS_SCRIPT_PROTOCOL.md` — required by §15 ("document clearly that the Apps Script must later be
updated to..."). Written as part of this stage, not after.

**Build here.**
