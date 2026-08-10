# Stage 12: Web UI

Spec §20, §21, §22, §23, §24. Vue 3 + Pinia + Tailwind + vue-i18n in `tiltbridge_web_ui/`.

Build: `cd tiltbridge_web_ui && npm ci && npm run build` — `tools/build_ui.py` copies the result
into `data/wifiui/` on `pio run -t buildfs`. The output is gzipped and currently ~24 KB; keep an
eye on it since it shares the 832 KB LittleFS partition with the queue (~192 KB at defaults).

## i18n

Every new string goes in `src/locales/en.json`. The other five locales (`de`, `es`, `nl`, `pt`
and the `translated_from/` mirrors) fall back to English when a key is missing, so **add English
only** and note the new keys for a later translation pass. Do not machine-translate here.

## §20 — Detected Devices page (`src/components/TiltList.vue`)

Current table columns: Color · RSSI · Battery age · Gravity · Temp · Last received.
Rows are keyed `:key="tilt.color"` (`TiltList.vue:37`) — **this breaks outright with two
same-color Tilts** (duplicate Vue keys). Change to `:key="tilt.deviceId"`.

Target layout (§20):

```
Name              Color   MAC                  Model        RSSI              Last Seen
Cabernet Tank 1   Red     88:C2:55:AC:26:81   Pro Family   (-70 dBm) GOOD    2 sec
Merlot Tank 4     Red     88:C2:55:AB:11:42   Standard     (-64 dBm) GOOD    1 sec
```

Keep Gravity, Temp and Battery age — they are the reason the page exists; §20's sketch shows the
*identity* columns, not an exhaustive list. Responsive classes already hide RSSI/Last-received
below `lg`; hide MAC and Model the same way and always show Name + Gravity + Temp.

Changes:

- **Name** column: `tilt.friendlyName` (falls back to the color name server-side), with the
  existing color swatch bar (`TiltList.vue:39`) still driven by `tilt.colorStyle`.
- **MAC** column: `tilt.deviceId`, monospace, `text-xs`.
- **Model** column: `tilt.modelLabel`.
- **RSSI** column: `({{ tilt.rssiLatest }} dBm) {{ tilt.rssiQuality }}` with a colour class from
  the quality string. **The thresholds stay in firmware** (`rssi_stats.h`); the UI only maps a
  name to a Tailwind class:
  ```js
  const rssiClass = (q) => ({
    EXCELLENT: 'text-green-700', GOOD: 'text-green-600', FAIR: 'text-yellow-600',
    WEAK: 'text-orange-600', CRITICAL: 'text-red-600',
  }[q] ?? 'text-gray-500');
  ```
  Tooltip (`:title`) shows `avg / min / max / samples` from the new fields.
- **Calibrate button** (`TiltList.vue:51`) currently routes to `/calibrate/${tilt.color}`. With
  per-device calibration it must route to the device: `/calibrate/${tilt.deviceId}`. Keep the
  color route working (`src/router/index.js`) so bookmarks and the color-only flow survive — the
  route param is just a string that the page forwards to the API as either `color` or `deviceId`.
- **Configure button**: new gear icon per row → opens the device-config modal (§21). Show a
  distinct icon state when `tilt.hasDeviceConfig` is false, so "this Tilt is still using shared
  color settings" is visible at a glance.
- When `combineTilts` is enabled, show a one-line notice above the table explaining that
  same-color Tilts are being merged and per-device configuration is unavailable
  (see `04-device-config.md`).

### `src/stores/TiltStore.js` and `src/mixins/TiltDevice.js`

`TiltDevice` is constructed positionally with 14 arguments (`TiltStore.js:28`) — adding six more
positional parameters is asking for a silent mis-ordering. **Refactor the constructor to take the
raw API object**:

```js
const tilt = new TiltDevice(tiltData);
```
and read named fields inside. Update `TiltDevice.js` accordingly, keeping every existing getter
(`colorStyle`, `formattedGravity`, …) intact. Check for other `new TiltDevice(` call sites first
(`grep -rn "new TiltDevice" src/`) — `CalibrationStore.js` may construct one too.

New fields consumed: `deviceId`, `friendlyName`, `modelLabel`, `enabled`, `hasDeviceConfig`,
`rssiLatest`, `rssiAverage`, `rssiMinimum`, `rssiMaximum`, `rssiSamples`, `rssiQuality`.

## §21 — Device configuration

New component `src/components/config/TiltBridge/DeviceConfigModal.vue`, opened from the row's gear
icon. New store `src/stores/DeviceConfigStore.js` wrapping `/api/devices/`.

Fields, all bound to the physical device (MAC), not the color:

| Field | Control | API key | Notes |
|---|---|---|---|
| Friendly name | `TextField` | `friendlyName` | max 32 |
| Enabled | `CheckboxField` | `enabled` | disabling stops sends, device still listed |
| Google Sheets name | `TextField` | `googleSheetsName` | max 25, matches `GsheetsConfig::name` |
| Manual model label | `TextField` | `modelLabel` | max 16, placeholder = auto-detected value |
| Notes | `TextField` | `notes` | max 64, optional |
| Repeater aliases | two `TextField`s | `aliases[]` | MAC format, optional |
| Calibration | link | — | routes to `/calibrate/<deviceId>` |

Reuse the existing form primitives in `src/components/config/form_elements/`
(`TextField.vue`, `CheckboxField.vue`, `SelectField.vue`) and the `UpdateSuccessfulModal.vue`
pattern for save feedback, so the page matches the rest of the app.

Show the read-only identity (MAC, detected color, detected model) at the top of the modal.
Include a **"Reset to color defaults"** action that calls `POST /api/devices/delete/` — that is
the escape hatch back to §3's fallback behaviour.

MAC validation client-side mirrors the firmware regex; the firmware validates regardless.

## §22 + §23 — Queue settings and status

New page/panel. The config area is `ConfigPage.vue` with tabs via `TabContainer.vue`; add an
**Offline Queue** section there, and put the *status* panel on the same page directly above the
settings so a user sees state and knobs together.

New store `src/stores/QueueStore.js`: `GET /api/queue/` on a 15 s interval (same cadence as
`TiltStore`), `PUT /api/settings/controller/` for the two settings,
`POST /api/queue/actions/` for the buttons.

### Settings (§22)

```
Offline Queue Snapshot Interval   [ 30 min ▾ ]   ( 10 / 15 / 30 / 60 / Custom… )
Maximum queued records            [ 1500      ]
Enable offline queue              [x]
Batch size                        [ 20 ]        (advanced)
```

The interval control is a `SelectField` with the four presets plus a `Custom…` option that reveals
a numeric `TextField` in minutes (§8 explicitly wants numeric configuration available). Convert
minutes ↔ seconds at the boundary; the API is in seconds
(`queueSnapshotIntervalSec`). Client-side range hints: 1–360 min, 100–3000 records — the firmware
clamps regardless.

Make the two settings visually distinct with the explanatory text from §22: the interval controls
*flash-write frequency*, the maximum controls *storage capacity*.

### Status panel (§23)

```
Offline queue

Queued readings:          37
Oldest reading:           4h 20m ago
Storage used:             18%
Maximum records:          1500
Snapshot interval:        30 min
Last successful upload:   2 min ago
Upload status:            IDLE
Dropped due to overflow:  0

[ Send backlog now ]   [ Clear queue ]
```

- Render `storagePercent` as a Tailwind progress bar; turn it amber above 75% and red above 90%.
- **`Dropped due to overflow` must be visually loud when non-zero** (§10: "never silently discard
  data") — red text plus a warning icon and a one-line explanation of what to change.
- Show `Time sync: not established` when `timeValid` is false, with a note that queued readings
  will be marked `TimestampValid: false` (§12).
- `Send backlog now` → `POST /api/queue/actions/ {"action":"sendBacklogNow"}`, then optimistically
  set `uploadStatus` to `SENDING` until the next poll.
- `Clear queue` → **confirmation modal required** (§23). Two-step: modal states how many records
  will be permanently destroyed, and the confirm button is disabled until the user checks an
  acknowledgement box. Sends `{"action":"clearQueue","confirm":true}`.

Format the age fields in the UI (`4h 20m ago`) from the raw seconds the API returns; there is an
existing precedent in `UptimeStatsPanel.vue` — reuse its formatting helper if it is exported,
otherwise add a small shared `formatAge(seconds)` util rather than duplicating logic in three
components.

## §24 — Sender status panel

Add to `About.vue`, next to `UptimeStatsPanel.vue` and `ResetActionsPanel.vue` — that page is
already the diagnostics home and shows reset reason and heap.

New store `src/stores/SenderHealthStore.js` polling `GET /api/sender/` every 10 s (faster than the
queue, because this is what the user watches when the device misbehaves).

```
Outbound sender

State:                IDLE
Current target:       None
Sender heartbeat:     1 sec ago
Send lock:            Free

Google Sheets
Last success:         2 min ago

Fermentrack
Last success:         2 min ago
```

Stuck state (§24):

```
State:               STALE
Current target:      Fermentrack
Request age:         83 sec
Send lock age:       83 sec
```

- Colour the `State` row: `IDLE`/`SENDING` neutral-green, `STALE` red.
- Show `Request age` / `Send lock age` rows **only** when a lock is held, so the idle panel stays
  short.
- Show `Consecutive failures` when non-zero.
- Show `Last recovery: sender heartbeat stale, 12 min ago` when the boot carried a recovery record
  (from `03-sender-recovery.md`). This is the single most useful line for confirming the fix in
  the field — make it prominent and keep it visible for the whole boot session.
- Show `Wi-Fi flag disagreements: N` when non-zero, with a short tooltip. If this counter climbs
  in the field, the stale-`wifi_cfg_is_connected()` diagnosis in `00-OVERVIEW.md` is confirmed.

§24 says explicitly: do not build a large diagnostics system. No charts, no history, no log
viewer — just these fields.

## Checklist

- [ ] `TiltList.vue` keyed by `deviceId`, new columns, gear button
- [ ] `TiltDevice.js` object-based constructor, all existing getters preserved
- [ ] `TiltStore.js` passes the raw object
- [ ] `DeviceConfigModal.vue` + `DeviceConfigStore.js`
- [ ] Calibration route accepts a deviceId as well as a color
- [ ] `QueueStore.js`, queue settings section, queue status panel, clear-queue confirm modal
- [ ] `SenderHealthStore.js`, sender status panel in `About.vue`
- [ ] All new strings in `en.json`
- [ ] `npm run build` clean, bundle size checked

**Build here** (firmware **and** `npm run build`).
