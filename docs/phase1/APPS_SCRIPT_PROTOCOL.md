# Google Apps Script protocol — enhanced queued mode (schemaVersion 2)

Required by spec §15. **The Apps Script currently published with TiltBridge does not implement
this protocol.** Do not assume it does. Enabling *Enhanced Google Sheets mode* in the TiltBridge
web UI without first updating the script will result in records never being acknowledged, and the
device will retry them forever (bounded only by the queue's overflow policy).

Legacy mode (`gsheetsV2Enabled = false`, the default) uses the existing single-reading protocol and
works with the existing unmodified script. Nothing here affects it.

---

## 1. Transport

- Method: `POST`
- URL: the user's Apps Script web-app URL (`config.scriptsURL`), unchanged from legacy mode
- `Content-Type: application/json`
- Firmware timeout: 15 s. Apps Script must respond within that window; if it does not, the device
  treats the attempt as failed and retries the same batch with the same record ids.
- Redirects are followed (Apps Script redirects to `script.googleusercontent.com`).

## 2. Request body

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

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | int | Always `2` for this protocol. Branch on it so one script can serve both protocols. |
| `deviceName` | string | The TiltBridge's mDNS id. |
| `Email` | string | Same value legacy mode sends per row; used to locate/share the spreadsheet. |
| `tzOffset` | int | Hours from UTC, −12..14. For display only — `CapturedAtUtc` is authoritative. |
| `readings` | array | 1 to `queueBatchSize` items, default max 20 (§14). Ordered oldest first. |
| `recordId` | string | `<bootId>-<deviceHash>-<sequence>`, e.g. `A91F2C-6A72-00000452`. **The dedup key.** Stable across retries and reboots (§11). |
| `deviceId` / `mac` | string | Canonical uppercase BLE address. Same value; both sent for convenience. |
| `Beer` | string | Per-device sheet name, snapshotted at capture time. |
| `Color` | string | `Red` … `Pink`. Not an identity — several rows may share a colour. |
| `Temp` | number | Degrees **Fahrenheit**, always. |
| `SG` | number | The normal final TiltBridge value (smoothed + calibrated + optionally temp-corrected). **Use this one.** |
| `SG_Raw` | number | Latest unsmoothed, uncalibrated reading. Diagnostic. |
| `SG_Smoothed` | number | Smoothed but uncalibrated. Diagnostic. |
| `RSSI` | int | Latest, dBm. |
| `RSSI_Avg` / `_Min` / `_Max` / `_Samples` | int | Aggregated over the snapshot interval (§4). Diagnostic only — **not** a battery indicator. |
| `CapturedAtUtc` | string | ISO-8601 UTC, `Z`-suffixed. **Absent when `TimestampValid` is false.** |
| `TimestampValid` | bool | `false` when the device had no trustworthy clock at capture (§12). |
| `UptimeMsAtCapture` | int | Present only when `TimestampValid` is false; milliseconds since that boot. Use with row order to sequence such rows. |
| `Comment` | string | Always empty in this phase; reserved. |

Numbers are JSON numbers, not strings — this differs from the legacy payload, which sends `Temp`
and `SG` as strings.

## 3. Response the script must return

```json
{
  "status": "ok",
  "acceptedRecordIds": [
    "A91F2C-6A72-00000452",
    "A91F2C-1234-00000453"
  ]
}
```

Rules the firmware enforces:

- **A record is deleted from the device queue only when its id appears in `acceptedRecordIds`.**
  HTTP 200 alone deletes nothing (§15).
- Ids may be returned in any order. Unknown ids are ignored.
- Returning a subset is legitimate and expected — the omitted records are retried in the next
  batch with the same ids.
- Return an already-processed id again (a duplicate submission) — that is the correct response
  and is how the device clears records whose acknowledgement was lost in transit.
- `status` may be `"ok"` or `"partial"`; the firmware treats any 2xx with a parseable
  `acceptedRecordIds` array the same way. Use `"error"` with an HTTP error status for hard
  failures.
- Keep the response under 2048 bytes. At 20 records that is ~600 bytes; do not echo the readings
  back.

Optional fields the firmware will store if present:

```json
{ "status": "ok", "acceptedRecordIds": [...], "doclongurl": "https://docs.google.com/..." }
```
`doclongurl` is cached per device and used for the spreadsheet link in the web UI, matching legacy
behaviour.

## 4. What the script must do

1. **Accept batches.** Parse `readings` as an array. Do not assume one reading per request.
2. **Store processed record ids.** Keep a durable set of every `recordId` already written. A
   dedicated hidden sheet (one id per row) or `PropertiesService` both work; a hidden sheet scales
   better past a few thousand ids and can be trimmed by age.
3. **Ignore duplicate record ids.** If an id is already in the set, do **not** append a second row —
   but **do** include it in `acceptedRecordIds` so the device stops retrying it.
4. **Return accepted record ids.** Only the ids actually persisted (or already known) belong in the
   array. If a row fails to write, leave its id out; the device will resend it.
5. Write rows in the order received so a backlog lands chronologically.
6. Handle `TimestampValid: false` deliberately — either write the row with a blank timestamp cell
   and a flag column, or write the receipt time into a separate "received at" column. **Do not**
   write the upload time into the capture-time column (§12).

Delivery semantics are:

```
at-least-once delivery (device)  +  duplicate suppression by recordId (script)
```

Neither half is optional. Without server-side dedup, a lost acknowledgement produces duplicate
rows.

## 5. Integrating with the script in this repository

`GoogleSheets/post_tilt.gs` is **not** the stock TiltBridge Apps Script. It is a customised
wine-fermentation script, and the v2 work has to fit into it rather than replace it. What it
currently does, and what each part means for v2:

| Existing behaviour | Location | Consequence for v2 |
|---|---|---|
| One reading per request, read from top-level `tiltData.Beer` / `.SG` / `.Temp` | `doPost` | v2 must branch on `schemaVersion === 2` and loop `readings[]`, reusing the same per-reading write logic |
| Row timestamp is `receivedAt = new Date()` | `doPost`, written into column A | **Must become `CapturedAtUtc` for v2**, otherwise a backlog uploads with today's time and §12 is violated. This is the single most important change. |
| `DEFAULT_LOG_INTERVAL_MINUTES` throttle via `shouldLogMeasurement()` | `doPost` | Must compare **capture** times, not receipt times. Applied naively to a batch it would discard every backlog row after the first, since they all arrive in one request. |
| Transmission-gap detection via `getStoredLastIncomingReading` / `rememberIncomingReading` | `doPost` | A backlog upload is not a gap. Update the remembered "last incoming" from the newest reading's capture time, and evaluate gaps between consecutive *captures* inside the batch. |
| Rolling 4-hour averages, previous-day averages, new-day shading | `calculateRollingAverageAssessment`, `calculateDailyAverages` | These walk the sheet backwards assuming chronological append order, and they must run per appended row using that row's capture time as "now", not the request time. **FIFO drain is not enough on its own** — see §5.2. |
| Missing-reading trigger every 15 min writing to `Monitoring` | `checkForMissingReadings` | Unchanged; it keys off the remembered incoming timestamp, which v2 keeps updating. |
| Per-wine sheet auto-creation, `sanitizeSheetName(wineForLog)` | `doPost` | Reuse as-is, keyed on each reading's `Beer`. A batch may span several wines, so resolve the sheet per reading, not per request. |
| Response `{status, code, logged, doclongurl}` via `createSuccessResponse` | `createSuccessResponse` | v2 response must add `acceptedRecordIds`. Keeping `doclongurl` is useful — the firmware caches it for the UI link. |
| `LockService` with a 6 s wait (`WEBAPP_LOCK_WAIT_MS`) | `doPost` | The firmware's v2 timeout is 15 s, so a 6 s lock wait still fits. But a 20-row batch does far more spreadsheet work than one row — measure it, and raise `WEBAPP_LOCK_WAIT_MS` toward 10 s if `WEBAPP_BUSY` starts appearing in `System Log`. |
| Durable error queue (`queuePendingLog` / `flushPendingLogs`) | throughout | Reuse for per-reading failures. A reading that fails to write should log **and** be left out of `acceptedRecordIds` so the device retries it. |
| Sheet layout: A date, B SG, C °C, D blank, E–G 4-hour avg, H–I previous-day avg | `prepareSheet` | v2 keeps A–I exactly where they are and adds a narrow diagnostic block at K–M — see §5.2. `recordId` must not be silently discarded; it is what dedup keys on. |
| Temperature converted F→C before writing | `doPost` | v2 sends `Temp` in Fahrenheit as a JSON number; keep the same conversion. |
| `TIME_ZONE = 'Europe/Zagreb'`, spreadsheet TZ forced to match | `ensureSpreadsheetTimeZone` | `CapturedAtUtc` is UTC. `new Date(r.CapturedAtUtc)` parses the `Z` suffix correctly and Sheets renders it in the spreadsheet timezone, so no manual offset arithmetic is needed — and none should be added. |

Additional required pieces, neither of which exists in the current script:

1. **A processed-id store.** A hidden `_processed_ids` sheet (or `PropertiesService` for small
   volumes). `PropertiesService` has a 9 KB per-value and 500 KB total limit, so at ~21 bytes per
   id a sheet is the safer choice past a few thousand records.
2. **`acceptedRecordIds` in the response**, built as the batch is processed.

### 5.1 Not every field the device sends deserves a column

The first v2 implementation widened the sheet from nine columns to nineteen by giving every
payload field a column of its own. That is the obvious thing to do and it is wrong: the result is
a wall of numbers that buries the three columns anyone actually reads.

The current layout is thirteen columns — A–I unchanged, then:

| | Column | Why it earns a column |
|---|---|---|
| K | Raw SG | genuinely different from B: pre-smoothing, so it is what you check calibration against |
| L | Signal dBm | the RSSI **average**; the one signal number worth charting |
| M | Record id | traces a row back to the exact device record that produced it |

What was dropped, and where it went instead:

- **Smoothed SG** — deleted outright. The payload's `SG` is `calibrate(smooth(raw))`, so column B
  *is* the smoothed value; the two differ only by the calibration polynomial and are identical on
  the default identity coefficients.
- **RSSI dBm / min / max / samples** — four columns became a **note on column L**. Detail is one
  hover away rather than four columns wide, and the same trick already carries the average-quality
  explanation on column G.
- **Device (MAC)** — removed from the rows entirely. Repeating an unchanging 17-character address
  on every row conveys nothing. It now lives in three places that each answer a different question:
  the **Monitoring** tab's `Device` column ("what is on this wine now?"), a note on the **Record id
  cell** of the row where it changed ("which Tilt produced this row?"), and a `DEVICE_CHANGED`
  entry in **System Log** ("when did it change, and from what?"). The first device a wine ever sees
  is recorded silently — there is no change to report.

Nothing is lost to the audit trail: `_processed_ids` still records every record id with its wine,
sheet, row, capture time, receipt time and outcome.

**There is no in-place migration.** `migrateLegacyLayoutIfNeeded()` keeps A, B and C — the only
columns every layout has ever agreed on, and the only ones that cannot be recomputed — clears
everything from D rightwards, and logs a `WARNING`. Everything discarded is either derived (E–I
are rebuilt from the raw readings as new rows arrive) or diagnostic detail whose absence costs
nothing. A visibly reset sheet beats a quietly misaligned one.

An in-place nineteen-to-thirteen narrowing did exist and was removed: the nineteen-column layout
only ever existed on this unmerged branch, so nothing in the wild needs it.

The note on a device change goes on column M rather than column A on purpose: column A's note is
owned by the data-gap marker, which `rebuildDerivedColumns()` clears and rewrites, and two owners
of one note is how notes get lost.

### 5.2 Derived values must be recomputed after a late upload

Each batch drains FIFO, so the rows *inside one batch* arrive in order. That is not the same as the
sheet being complete when a derived value is computed, and the difference is where the original
implementation was wrong.

The failing case is a partial outage rather than a total one. The device keeps uploading, some
requests succeed and some do not, and the ones that fail go to the queue:

```
10:00  uploaded                        row written
10:10 … 11:30  upload failed           queued on the device
11:40  uploaded                        row written — the sheet now has a 90-minute hole
12:00  four-hour average point due     computed over a window containing that hole
                                       → "INCOMPLETE — 1h 30m gap"
12:10  queue drains                    10:10 … 11:30 finally arrive and sort into place
                                       → the hole is gone, the label is not
```

The queue did its job and the data ended up complete; the sheet still claimed otherwise, forever,
because nothing revisited a row once it was written.

Everything computed from a *window* of rows has this shape:

| Derived output | Column | How a late upload invalidates it |
|---|---|---|
| 4-hour rolling average SG / °C | E, F | averaged over fewer readings than the window now holds |
| Average quality | G | `INCOMPLETE` / `INSUFFICIENT DATA` over a window that is now complete |
| Where the 4-hour points fall | E–G | spacing chains off the previous point, so a backfilled row can be the one that is actually due |
| Previous-day average SG / °C | H, I | written at the first row of a new day, before late rows for the previous day arrive |
| New-day shading | row fill | a backfilled row can become the true first row of its day |
| `DATA GAP` fill and note | A–C | the gap it marks may since have been filled in |

There is no 12-hour or other longer window in this script — the 4-hour rolling average and the
previous calendar day are the only two, and both are covered above.

**The fix.** `sortWineSheetByCaptureTime` already runs after every batch and already reports the
first row it moved. A reorder is exactly the signal that rows landed somewhere other than the end
of the sheet, and the sheet is sorted, so no row *above* that point can have a backfilled row
inside its rolling window or its previous calendar day. So the first moved row is a tight lower
bound on what is stale, and `rebuildDerivedColumns()` recomputes E–I, the fills and the notes from
that row down, from columns A–C as they now stand.

Properties worth preserving in any reimplementation:

- **Recompute, do not patch.** Every derived value is a pure function of A, B, C over a window.
  The rebuild throws the old values away rather than trying to adjust them.
- **Oldest first.** The 4-hour spacing chains forward from the previous average point, so a tail
  cannot be rebuilt before the rows above it are correct. The rebuild seeds itself from the last
  average point *above* its window, which is why the untouched part of the sheet never shifts.
- **Write only on change.** The rows below a small backfill usually recompute to exactly what they
  already say. Comparing before writing keeps the normal case free.
- **Bounded inside a request.** The firmware's v2 HTTP timeout is 15 s. The in-request pass is
  capped at `REBUILD_MAX_ROWS_PER_REQUEST` rows; the remainder is queued in a Script Property and
  finished by the 15-minute background trigger, which has no timeout on the other end.
- **`DATA GAP` is only ever cleared, never added.** The live path measures gaps between readings
  *seen*, including ones the logging interval declined to store; a rebuild only sees stored rows.
  Clearing a flag whose gap has since been filled is always right. Adding one from a coarser view
  of the same history is not.

Run `rebuildAllDerivedColumns()` once after deploying this version: sheets written by the previous
version still carry averages and quality marks computed before the queue delivered what they were
missing, and nothing else clears them.

### Handling `TimestampValid: false`

The script's entire model — gap detection, rolling averages, daily averages, new-day shading —
is driven by column A being a real capture time. A reading with no trustworthy timestamp cannot
participate in that. Recommended handling: write it to the sheet with column A blank and a cell
note explaining why, keep it out of the average calculations, and record it in `System Log` at
`WARNING` with code `NO_VALID_TIMESTAMP`. Acknowledge it either way so the device stops retrying.

### Reference skeleton

Generic sketch of the v2 branch only, to illustrate the acknowledgement contract. It is **not** a
drop-in for `post_tilt.gs` — that integration is described in the table above.

```javascript
const ID_SHEET = '_processed_ids';
const ID_RETENTION = 20000;   // trim the id log beyond this many rows

function doPost(e) {
  const body = JSON.parse(e.postData.contents);

  if (body.schemaVersion !== 2) {
    return legacyDoPost(body);          // keep the existing single-reading path
  }

  const ss       = getOrCreateSpreadsheet(body.Email, body.deviceName);
  const idSheet  = getOrCreateIdSheet(ss);
  const known    = new Set(idSheet.getDataRange().getValues().flat());
  const accepted = [];

  (body.readings || []).forEach(function (r) {
    if (!r.recordId) { return; }

    if (known.has(r.recordId)) {
      accepted.push(r.recordId);        // already stored — acknowledge so the device drops it
      return;
    }

    try {
      const sheet = getOrCreateBeerSheet(ss, r.Beer || r.Color);
      sheet.appendRow([
        r.TimestampValid ? new Date(r.CapturedAtUtc) : '',
        r.TimestampValid,
        r.SG, r.Temp, 'F',
        r.Color, r.deviceId, r.Beer,
        r.RSSI, r.RSSI_Avg, r.RSSI_Min, r.RSSI_Max, r.RSSI_Samples,
        r.recordId,
        new Date()                       // received-at, never the capture column
      ]);
      idSheet.appendRow([r.recordId]);
      known.add(r.recordId);
      accepted.push(r.recordId);
    } catch (err) {
      // Leave this id out of `accepted` — the device will retry it with the same id.
      console.error('row failed for ' + r.recordId + ': ' + err);
    }
  });

  trimIdSheet(idSheet, ID_RETENTION);

  return ContentService
      .createTextOutput(JSON.stringify({ status: 'ok', acceptedRecordIds: accepted }))
      .setMimeType(ContentService.MimeType.JSON);
}
```

Notes on the sketch:

- Reading the whole id sheet on every request is O(n). Past a few thousand ids, cache the set in
  `CacheService` keyed by spreadsheet id, or index by `bootId` prefix.
- `LockService.getScriptLock()` around the write section prevents interleaved executions from
  double-appending when the device retries quickly.
- Apps Script's 6-minute execution limit is not a concern at 20 rows per request, but do not raise
  `queueBatchSize` far above the default without checking.

## 6. Migration checklist for a user enabling enhanced mode

1. Update the Apps Script to a version implementing this document; deploy a **new version** of the
   web app (Apps Script serves the last deployed version, not the last saved code). Run
   `rebuildAllDerivedColumns()` once from the editor to repair averages and quality marks that
   earlier versions left stale (§5.2).
2. Confirm the deployment URL is unchanged, or paste the new one into TiltBridge.
3. Send a manual test batch (curl the URL with a one-reading v2 body) and confirm the response
   contains `acceptedRecordIds`.
4. Only then tick **Enhanced Google Sheets mode** in the TiltBridge UI.
5. Watch the queue panel: `Queued readings` should drain to 0 and `Last successful upload` should
   update.

If step 5 shows the count climbing instead, the script is not acknowledging — turn enhanced mode
back off, and the device reverts to the legacy path with the queue retained for later.
