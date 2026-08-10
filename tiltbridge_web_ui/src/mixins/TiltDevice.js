
export const TiltColors = [
    "Red",
    "Green",
    "Black",
    "Purple",
    "Orange",
    "Blue",
    "Yellow",
    "Pink"
]

export const TiltColorHTML = [
    "bf3636",  // Red
    "579d42",  // Green
    "333f48",  // Black (displays as dark grey)
    "551155",  // Purple
    "bf5700",  // Hook 'em Horns!
    "005f86",  // Blue
    "ffd600",  // Yellow
    "f7a399"   // Pink
]

// The RSSI thresholds themselves live in firmware (src/rssi_stats.h) so they exist in exactly
// one place. The UI only maps the quality name the firmware reports to a Tailwind class.
const RSSI_QUALITY_CLASSES = {
    EXCELLENT: 'text-green-700',
    GOOD: 'text-green-600',
    FAIR: 'text-yellow-600',
    WEAK: 'text-orange-600',
    CRITICAL: 'text-red-600',
};

export function rssiQualityClass(quality) {
    return RSSI_QUALITY_CLASSES[quality] ?? 'text-gray-500';
}

/**
 * Matches the firmware's device id regex (src/device_config.cpp). The firmware validates
 * regardless - this only keeps obviously broken input from being sent.
 */
export const MAC_REGEX = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/;

export function isValidMac(value) {
    return typeof value === 'string' && MAC_REGEX.test(value.trim());
}


export class TiltDevice {
    color;
    temp;
    raw_temp;
    temp_unit;
    gravity;
    raw_gravity;
    weeks_on_battery;
    sends_battery = false;
    high_resolution = false;
    fw_version;
    rssi;
    gsheets_name = "";
    gsheets_link = "";
    lastReceived = 0;

    // Per-device identity (added by the Beta 5 device-config work). Two Tilts of the same
    // colour are only distinguishable by these.
    deviceId = "";
    mac = "";
    friendlyName = "";
    modelLabel = "";
    enabled = true;
    hasDeviceConfig = false;

    // RSSI aggregates over the firmware's sampling interval.
    rssiLatest = null;
    rssiAverage = null;
    rssiMinimum = null;
    rssiMaximum = null;
    rssiSamples = 0;
    rssiQuality = "";

    /**
     * Built directly from one entry of the /api/json/ "tilts" array.
     *
     * This used to take 14 positional arguments; the device-config and RSSI work added six
     * more fields, at which point positional construction was a silent mis-ordering waiting
     * to happen. Pass the raw API object instead.
     */
    constructor(data) {
        const d = data || {};

        this.color = d.color;
        this.temp = d.temp;
        this.temp_unit = d.tempUnit ?? d.temp_unit;
        // The dashboard shows the calibrated value; the calibration page uses raw_gravity.
        this.gravity = d.calibratedGravity ?? d.gravity;
        this.raw_temp = d.raw_temp ?? 0;
        this.raw_gravity = d.latestGravity ?? d.uncalibratedGravity ?? d.gravity;
        this.weeks_on_battery = d.weeks_on_battery;
        this.sends_battery = d.sends_battery ?? false;
        this.high_resolution = d.high_resolution ?? false;
        this.fw_version = d.fwVersion ?? d.fw_version;
        this.rssi = d.rssi;
        this.gsheets_name = d.gsheets_name ?? "";
        this.gsheets_link = d.gsheets_link ?? "";
        this.lastReceived = d.lastReceived ?? 0;

        this.deviceId = d.deviceId ?? d.mac ?? "";
        this.mac = d.mac ?? d.deviceId ?? "";
        // Falls back to the colour name server-side, but guard for older firmware too.
        this.friendlyName = d.friendlyName ?? d.color ?? "";
        this.modelLabel = d.modelLabel ?? "";
        this.enabled = d.enabled ?? true;
        this.hasDeviceConfig = d.hasDeviceConfig ?? false;

        this.rssiLatest = d.rssiLatest ?? d.rssi ?? null;
        this.rssiAverage = d.rssiAverage ?? null;
        this.rssiMinimum = d.rssiMinimum ?? null;
        this.rssiMaximum = d.rssiMaximum ?? null;
        this.rssiSamples = d.rssiSamples ?? 0;
        this.rssiQuality = d.rssiQuality ?? "";
    }

    get colorHTML() {
        // Returns the HTML color code for the Tilt color
        return TiltColorHTML[TiltColors.indexOf(this.color)];
    }

    get colorStyle() {
        // Return a style string that can be used to set the background color of an element
        return "background-color: #" + this.colorHTML + ";";
    }

    get colorIndex() {
        return TiltColors.indexOf(this.color);
    }

    /**
     * Stable per-row key. Colour is NOT unique - two same-colour Tilts produce duplicate Vue
     * keys - so prefer the device id and only fall back for pre-Beta-5 firmware.
     */
    get rowKey() {
        return this.deviceId || this.mac || this.color;
    }

    /**
     * What the calibration route should be pointed at: the physical device when we know it,
     * the colour otherwise (which keeps the colour-only flow working).
     */
    get calibrationTarget() {
        return this.deviceId || (this.color ? this.color.toLowerCase() : "");
    }

    get rssiQualityClass() {
        return rssiQualityClass(this.rssiQuality);
    }

    formattedGravity(gravityUnit) {
        const sg = parseFloat(this.gravity);
        if (gravityUnit === "P") {
            const plato = -616.868 + 1111.14 * sg - 630.272 * sg * sg + 135.997 * sg * sg * sg;
            return plato.toFixed(1) + " \u00B0P";
        }
        if (gravityUnit === "B") {
            const brix = (261.1 * (sg - 1.0) + 1.0) / 1.04;
            return brix.toFixed(1) + " \u00B0Bx";
        }
        return sg.toFixed(4);
    }
}
