# The Google Apps Script side — `GoogleSheets/post_tilt.gs`

This is not the stock TiltBridge script. It is a customised wine-fermentation script:
rolling four-hour averages, previous-day averages, gap detection, a Monitoring tab, a
System Log, and duplicate suppression keyed on the record ids the firmware sends.

It runs on Google's servers. **A firmware build never touches it, and nothing in CI
checks it** — which is why the mock harness below exists.

---

## 1. Deploying a change

The file in this repo is the source of truth, but editing it changes nothing on its own.

1. Open the Apps Script project bound to the spreadsheet.
2. Replace the contents of the script file with `GoogleSheets/post_tilt.gs`.
3. **Deploy → Manage deployments → edit the active Web app deployment → Deploy.**
   Saving alone does not update the `/exec` URL the device posts to.
4. The device's `scriptsURL` must point at that deployment. If you create a *new*
   deployment instead of editing the existing one, the URL changes and the device keeps
   posting to the old one — which silently keeps working against stale code.

Check it landed: the next upload's response should come back with the expected
`acceptedRecordIds`, and `System Log` gets a `LAYOUT_WIDENED` or similar entry if the
layout version changed.

---

## 2. Settings you are expected to tune

### `EXPECTED_READING_INTERVAL_MINUTES`

**Set this to the device's Google Sheets push interval in minutes**
(`gsheetsPushEvery / 60`, visible at `GET /api/settings/json/`).

Everything about data-quality grading scales from it:

| Derived from it | Multiple | At 15 min | At 30 min |
|---|---|---|---|
| `AVERAGE_COMPLETE_MAX_GAP_MINUTES` | 1.5× (floor 30) | 30 | 45 |
| `AVERAGE_INCOMPLETE_MAX_GAP_MINUTES` | 3× (floor 60) | 60 | 90 |
| `MISSING_READING_MINUTES` | 2.5× (floor 60) | 60 | 75 |

Leaving it at the wrong value is not cosmetic. These were absolute minutes tuned for a
10-minute cadence, and at a 30-minute interval a window with **nothing missing** sat
exactly on the `COMPLETE` boundary — so ordinary jitter reported `INCOMPLETE` and one
missed reading reported `INSUFFICIENT DATA`. The metric graded the configured interval
rather than whether data was actually missing.

If you lengthen the device's interval and forget this, the symptom is average cells
turning amber and red for no reason.

**Do not replace this with auto-detection from the readings.** Estimating cadence from
the same window being graded makes the metric self-fulfilling: during a real outage the
observed spacing grows, so a half-empty window would grade `COMPLETE`.

### `DEFAULT_LOG_INTERVAL_MINUTES`

How often a row is *saved*, independent of how often the device uploads. Note
`shouldLogMeasurement()` short-circuits to `true` at `<= 10`, so at the default of 10 it
is inert — every upload becomes a row.

### `BATCH_MAX_READINGS`

Caps one request at 40. If the firmware's `queueBatchSize` is raised above 40, raise this
too or the tail of each batch is silently ignored.

---

## 3. Wine sheet layout — twelve columns

```
A  Date and time         capture time, never upload time
B  SG                    final calibrated, smoothed
C  Temperature °C
D  (separator)
E  4-hour avg SG     \   quality is the FILL on these two, bold,
F  4-hour avg °C     /   with the detail in a note on E
G  Previous day avg SG
H  Previous day avg °C
I  (separator)
J  Raw SG            \   schemaVersion 2 only; blank on the
K  Signal dBm         |  single-reading legacy path
L  Record id         /
```

Charts anchor at `CHART_COLUMN` = `DATA_COLUMN_COUNT + 2` = 14, one clear column past the
data, so they cannot overlap it regardless of width.

**There is no "Average quality" column.** It used to be column G holding text like
`INCOMPLETE — 45m gap`, which was a full column blank on roughly seven rows in eight and
describing the two cells beside it. It is now the background fill on E:F plus a note on
E — same three signals (colour, emphasis, detail on hover), one column narrower.

### Changing the layout

`LAYOUT_VERSION` gates re-preparation. `ensureSheetPrepared()` skips `prepareSheet()` once
`sheet-layout-<LAYOUT_VERSION>-<sheetId>` is set, so **editing `prepareSheet` alone has no
effect on existing sheets** — the version string is what invalidates it.

A sheet on an older layout keeps **A/B/C** (capture time, SG, temperature) and has D
onward cleared and recomputed by the rebuild path. Raw measurements are never at risk;
derived values are regenerated. Expect one `LAYOUT_WIDENED` entry in System Log per sheet.

If you add or remove a column you must update, in step:
`DATA_COLUMN_COUNT`, `WINE_SHEET_HEADERS`, `buildMeasurementRowValues()`,
`prepareSheet()` (A1-notation formats, separator clears, column widths),
`rebuildDerivedColumns()` (the derived range width and the `rowBackground` indices), and
`LAYOUT_VERSION`. The test suite below catches the common half-done version.

---

## 4. Testing it without deploying

```bash
node GoogleSheets/test/run_tests.js
```

`GoogleSheets/test/mock_apps_script.js` mocks `SpreadsheetApp`, `PropertiesService`,
`LockService`, `Utilities`, `ContentService` and `Session` well enough to load the real
file and call its functions. The sheet model keeps values, notes, backgrounds, font
weights and number formats, because the script uses all of them as real per-row state.

`run_tests.js` asserts the things that are cheap to get wrong and expensive to find on a
live sheet:

- headers, `DATA_COLUMN_COUNT` and `buildMeasurementRowValues()` all agree on the width
- charts anchor clear of the data
- separator columns are actually blank in a built row
- a *perfect* window grades `COMPLETE` at the configured cadence, with 25% jitter still
  `COMPLETE`, one missed reading at worst `INCOMPLETE`, and a 4× gap `INSUFFICIENT`

A syntax check alone is also worth running after any edit, since a load-time error takes
the whole endpoint down:

```bash
cp GoogleSheets/post_tilt.gs /tmp/pt.js && node --check /tmp/pt.js
```

**Top-level `const` is script-scoped**, so a constant referenced above its declaration
throws a `ReferenceError` at load — the temporal dead zone. The order of the constant
block is load-bearing; `MISSING_READING_MINUTES` depends on
`EXPECTED_READING_INTERVAL_MINUTES` being declared first.

---

## 5. Performance — why an upload takes ~10 s

Measured by splitting 52 uploads at the 302 redirect:

| Leg | What it is | min | max | avg |
|---|---|---|---|---|
| POST → 302 | Apps Script running this file | 6 s | 22 s | 10.6 s |
| 302 → body | second TLS handshake + echo fetch | 1 s | 7 s | 2.3 s |

All the variance is Google's leg; the device's own work is a steady 1–3 s. **Upload
latency is not a firmware problem.** At roughly 150–200 ms per Sheets service call and
~56 calls per request, ~9.5 s is expected. The multiplier is that each Tilt writes to its
own sheet.

The four worthwhile reductions are listed in
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) — chiefly an unconditional `setBackground(null)` on
every appended row, three separate `getRange()` calls on the same row, and a full
column-A read per sheet per request to check an ordering that is essentially never
violated in steady state.

**Do not "optimise"** the chunked backward scans (500-row chunks with early exit) or the
batching of all readings into one request. Both are already right.

---

## 6. Things that will bite you

- **The device's timeout is 60 s** (`GSHEETS_V2_TIMEOUT_MS`) while
  `senderStaleRebootSec` is 90 s. A request that hangs to its full timeout leaves only
  30 s of margin before the health monitor treats the stalled heartbeat as grounds for a
  recovery reboot. Anything that makes this script slower eats that margin.
- **Readings are acknowledged, not just written.** The device drops a record from its
  queue only when its id comes back in `acceptedRecordIds`. A response that is a 200 with
  an unparseable body, or lacks that array, acknowledges nothing and the identical batch
  is retried under the same ids — which `_processed_ids` then absorbs as duplicates.
- **A reading with a missing or non-numeric `SG`/`Temp` is acknowledged, not retried.**
  Deliberate: strict at-least-once would retry it forever and pin a queue slot on a record
  that can never become valid. It is logged `INVALID_READING`.
- **`_processed_ids` is the dedup source of truth.** Clearing it will duplicate rows on
  the next retry.
- **Capture time, not upload time.** Under schemaVersion 2 every timestamp and every gap
  calculation uses the reading's capture time, so a backlog uploaded hours late still
  lands where it belongs chronologically — which is also why gap thresholds must be
  compared against the *capture* cadence.
