
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

    constructor(color, temp, temp_unit, gravity, weeks_on_battery, sends_battery, high_resolution, fw_version, rssi, gsheets_name, gsheets_link, raw_temp, raw_gravity, lastReceived) {
        this.color = color;
        this.temp = temp;
        this.temp_unit = temp_unit;
        this.gravity = gravity;
        this.weeks_on_battery = weeks_on_battery;
        this.sends_battery = sends_battery;
        this.high_resolution = high_resolution;
        this.fw_version = fw_version;
        this.rssi = rssi;
        this.gsheets_name = gsheets_name;
        this.gsheets_link = gsheets_link;
        this.raw_temp = raw_temp;
        this.raw_gravity = raw_gravity;
        this.lastReceived = lastReceived || 0;
    }

    get colorHTML() {
        // Returns the HTML color code for the Tilt color
        return TiltColorHTML[TiltColors.indexOf(this.color)];
    }

    get colorStyle() {
        // Return a style string that can be used to set the background color of an element
        return "background-color: #" + this.colorHTML + ";";
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
