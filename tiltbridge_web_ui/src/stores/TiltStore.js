import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";
import { TiltDevice, TiltColors } from "@/mixins/TiltDevice";
import { useWifiLinkStore } from "@/stores/WifiLinkStore";

export const useTiltStore = defineStore("TiltStore", () => {

    const tilts = ref([]);
    const gravityUnit = ref("SG");
    const loaded = ref(false);
    const tiltsError = ref(false);
    const tiltUpdateError = ref(false);

    async function getTilts() {
        const remote_api = mande("/api/json/", genCSRFOptions());
        const response = await remote_api.get();
        if (response) {
            await clearTilts();

            // Response is { tilts: [...], gravityUnit: "SG"|"P"|"B" }
            const tiltArray = response.tilts || response;
            gravityUnit.value = response.gravityUnit || "SG";

            for (const tiltKey in tiltArray) {
                // TiltDevice reads the named fields it needs straight off the API object.
                const tilt = new TiltDevice(tiltArray[tiltKey]);
                tilts.value.push(tilt);
            }

            /*
             * /api/json/ also carries the WiFi uplink report, so the homepage's signal
             * indicator rides on this poll instead of issuing one of its own. Guarded
             * because a UI build can be served by firmware that predates the `wifi` key.
             */
            if (response.wifi) {
                useWifiLinkStore().applyPayload(response.wifi);
            }

            loaded.value = true;
            tiltsError.value = false;
        } else {
            // We weren't able to get a response.
            // TODO - Figure out what I want to do here
            tiltsError.value = true;
        }
    }

    async function clearTilts() {
        tilts.value = [];
        gravityUnit.value = "SG";
        loaded.value = false;
        tiltUpdateError.value = false;
    }

    /**
     * Look a Tilt up by whatever identifier we happen to have. Routes and modals carry either
     * a canonical device id ("88:C2:55:AC:26:81") or a colour name, so both are accepted.
     */
    function findTilt(identifier) {
        if (!identifier) return undefined;
        const needle = String(identifier).toLowerCase();
        return tilts.value.find((tilt) => {
            if (tilt.deviceId && tilt.deviceId.toLowerCase() === needle) return true;
            if (tilt.mac && tilt.mac.toLowerCase() === needle) return true;
            return !!tilt.color && tilt.color.toLowerCase() === needle;
        });
    }

    function getColorNumber(colorName) {
        const colorMap = {
            'red': 0,
            'green': 1,
            'black': 2,
            'purple': 3,
            'orange': 4,
            'blue': 5,
            'yellow': 6,
            'pink': 7
        };
        return colorMap[colorName.toLowerCase()];
    }

    function getColorName(colorNumber) {
        const colorNames = ['Red', 'Green', 'Black', 'Purple', 'Orange', 'Blue', 'Yellow', 'Pink'];
        return colorNames[colorNumber] || 'Unknown';
    }

    return {
        tilts,
        gravityUnit,
        loaded,
        tiltsError,
        tiltUpdateError,

        getTilts,
        clearTilts,
        findTilt,
        getColorNumber,
        getColorName
    };
});
