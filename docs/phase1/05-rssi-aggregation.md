# Stage 7: RSSI aggregation and quality classification

Spec §4, §5.

## What exists

`tiltHydrometer::rssi` (`tiltHydrometer.h:65`, `int8_t`) holds only the latest value, assigned in
`set_values()` (`tiltHydrometer.cpp:207`). It is already surfaced as `j["rssi"]`
(`tiltHydrometer.cpp:299`). `tiltScanner::load_tilt_from_advert_hex()` reads
`advertisedDevice->getRSSI()` (`tiltScanner.cpp:102`) on **every** advert, so the sample source is
already at the right frequency — a Tilt advertises roughly every 1–5 s and the scanner runs a
3 s window per `loop()` pass.

## New file: `src/rssi_stats.h` (header-only)

```cpp
#ifndef TILTBRIDGE_RSSI_STATS_H
#define TILTBRIDGE_RSSI_STATS_H

#include <cstdint>

// Per-device RSSI statistics for the interval between persistent queue snapshots (§4).
// Overflow-safe: int32_t sum with a uint16_t sample cap. Worst case is
// 65535 * -128 = -8,388,480, which is ~0.4% of INT32_MIN — no overflow possible.
struct RssiStats {
    int8_t   latest  = 0;
    int8_t   minimum = 0;       // most negative seen (weakest)
    int8_t   maximum = 0;       // least negative seen (strongest)
    int32_t  sum     = 0;
    uint16_t samples = 0;
    bool     hasData = false;

    void add(int8_t rssi) {
        latest = rssi;
        if (!hasData) {
            minimum = maximum = rssi;
            hasData = true;
        } else {
            if (rssi < minimum) minimum = rssi;
            if (rssi > maximum) maximum = rssi;
        }
        // Saturate rather than wrap. 65535 samples is ~18 hours at one advert/second,
        // far longer than any snapshot interval, so this only ever trips on a stuck clock.
        if (samples < UINT16_MAX) {
            sum += rssi;
            samples++;
        }
    }

    int8_t average() const {
        if (samples == 0) return latest;
        // Round toward zero is fine for a diagnostic dBm figure.
        return (int8_t)(sum / (int32_t)samples);
    }

    // §4: reset the interval statistics after a snapshot has been created.
    // `latest` deliberately survives the reset so the UI never shows a blank value.
    void resetInterval() {
        sum = 0;
        samples = 0;
        minimum = maximum = latest;
        hasData = (latest != 0);
    }
};

// §5 — diagnostic only. Never used for battery estimation (§5 last line, §29).
enum class RssiQuality : uint8_t { EXCELLENT, GOOD, FAIR, WEAK, CRITICAL };

inline RssiQuality rssiQuality(int8_t rssi) {
    if (rssi >= -59) return RssiQuality::EXCELLENT;   // >= -59 dBm
    if (rssi >= -71) return RssiQuality::GOOD;        // -60 .. -71
    if (rssi >= -79) return RssiQuality::FAIR;        // -72 .. -79
    if (rssi >= -88) return RssiQuality::WEAK;        // -80 .. -88
    return RssiQuality::CRITICAL;                     // < -88
}

inline const char* rssiQualityName(int8_t rssi) {
    switch (rssiQuality(rssi)) {
        case RssiQuality::EXCELLENT: return "EXCELLENT";
        case RssiQuality::GOOD:      return "GOOD";
        case RssiQuality::FAIR:      return "FAIR";
        case RssiQuality::WEAK:      return "WEAK";
        default:                     return "CRITICAL";
    }
}

#endif
```

Boundary check against the spec table: `-59 → EXCELLENT`, `-60 → GOOD`, `-71 → GOOD`,
`-72 → FAIR`, `-79 → FAIR`, `-80 → WEAK`, `-88 → WEAK`, `-89 → CRITICAL`. The spec's examples
(`-58 EXCELLENT`, `-67 GOOD`, `-76 FAIR`, `-84 WEAK`, `-91 CRITICAL`) all land correctly.

## Wiring

### `src/tilt/tiltHydrometer.h`

```cpp
#include "rssi_stats.h"
...
RssiStats rssi_stats;
```
Keep the existing `int8_t rssi;` member — it is read in `to_json()` and by MQTT
(`targets/mqtt.cpp`), and `rssi_stats.latest` duplicates it harmlessly. Simplest is to leave
`rssi` as the canonical latest and have `to_json` read stats for the aggregates.

### `src/tilt/tiltHydrometer.cpp` — `set_values()`

Add next to the existing `rssi = current_rssi;` (`:207`):
```cpp
rssi = current_rssi;
rssi_stats.add(current_rssi);
```

Careful with the **early return at `:118`**: when `i_temp == 999` the function returns before
reaching `:207`, so version-code adverts currently do not record RSSI. Those are still real
adverts from the device and should count. Move the RSSI capture to the **top** of `set_values()`,
before the `i_temp == 999` branch:

```cpp
bool tiltHydrometer::set_values(uint16_t i_temp, uint16_t i_grav, uint8_t i_tx_pwr, int8_t current_rssi)
{
    rssi = current_rssi;
    rssi_stats.add(current_rssi);
    // ... existing body, with the old `rssi = current_rssi;` line at :207 removed
```

This is a behaviour improvement (more samples) and does not affect gravity.

### `src/tilt/tiltHydrometer.cpp` — `to_json()`

Inside the `if (!legacy_keys)` branch (see `04-device-config.md` on payload size), add:

```cpp
j["rssiLatest"]  = rssi;
j["rssiAverage"] = rssi_stats.average();
j["rssiMinimum"] = rssi_stats.minimum;
j["rssiMaximum"] = rssi_stats.maximum;
j["rssiSamples"] = rssi_stats.samples;
j["rssiQuality"] = rssiQualityName(rssi);
```

Keep the existing `j["rssi"]` key untouched — `TiltList.vue` / `TiltDevice.js` may already read it.

### Reset point (§4: "reset the interval statistics after the snapshot has been created")

The only caller of `resetInterval()` is the queue snapshot builder in stage 8, immediately after
the record has been **successfully persisted** — not merely built. If the flash write fails, keep
accumulating so the next snapshot still covers the whole interval. Sequence in
`06-persistent-queue.md`:

```
build record (copies current stats)
  -> persist record to flash
     -> on success: th.rssi_stats.resetInterval()
     -> on failure: leave stats alone, log, count the failure
```

### Where the stats live across a reboot

They do not. `RssiStats` is RAM-only and resets on boot — acceptable, since it describes the
current snapshot interval and §25 explicitly forbids continuously persisting RSSI. The values
already written into queued records survive because they are part of the record.

## UI

Covered in `09-web-ui.md`. The display format is `(-58 dBm) EXCELLENT` (§5) and is built in the
UI from `rssiLatest` + `rssiQuality` so the classification thresholds live in exactly one place
(firmware). Do not reimplement the thresholds in JavaScript.

**Build here.**
