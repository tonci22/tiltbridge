#ifndef TILTBRIDGE_RSSI_STATS_H
#define TILTBRIDGE_RSSI_STATS_H

#include <cstdint>

/**
 * @brief Per-device RSSI statistics for the interval between persistent queue snapshots.
 *
 * Overflow-safe by construction: the accumulator is an int32_t and the sample count is
 * capped at UINT16_MAX, so the worst case is 65535 * -128 = -8,388,480 - about 0.4% of
 * INT32_MIN. RAM-only; these describe the current snapshot interval and are deliberately
 * never persisted (continuously writing RSSI to flash is exactly what the spec forbids).
 */
struct RssiStats {
    int8_t   latest  = 0;
    int8_t   minimum = 0;       // most negative seen (weakest signal)
    int8_t   maximum = 0;       // least negative seen (strongest signal)
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

        // Saturate rather than wrap. 65535 samples is ~18 hours at one advert per second,
        // far longer than any snapshot interval, so this only trips on a stuck timer.
        if (samples < UINT16_MAX) {
            sum += rssi;
            samples++;
        }
    }

    int8_t average() const {
        if (samples == 0) return latest;
        return (int8_t)(sum / (int32_t)samples);
    }

    /**
     * @brief Start a new interval. Called only after a snapshot has been durably persisted.
     *
     * `latest` deliberately survives so the UI never shows a blank current value.
     */
    void resetInterval() {
        sum = 0;
        samples = 0;
        minimum = maximum = latest;
        hasData = (latest != 0);
    }
};

// Diagnostic signal-quality bands. These are never used to infer battery state.
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

#endif // TILTBRIDGE_RSSI_STATS_H
