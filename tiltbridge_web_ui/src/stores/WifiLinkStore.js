import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from 'vue';

/**
 * Health of the WiFi uplink between the router and the TiltBridge.
 *
 * Deliberately fed from two places. The firmware emits the identical object on
 * GET /api/network/ and nested under `wifi` in GET /api/json/, so the homepage - which
 * already polls /api/json/ every 15 s for the Tilt list - can drive its header indicator
 * through applyPayload() without adding a request, while the About panel polls
 * /api/network/ directly. Both land in this one store, so the two views cannot disagree.
 */
export const useWifiLinkStore = defineStore("WifiLinkStore", () => {
    // Link state. `associated` is the driver's answer (are we on an access point);
    // `managerConnected` is what the firmware's WiFi manager believes. They are supposed to
    // agree - see the note on wifiDesynced below for why both are carried.
    const associated = ref(false);
    const managerConnected = ref(false);
    /*
     * WiFi-manager desynchronisation, as episodes and durations rather than the old per-poll
     * counter. SenderHealthPanel shows the same figures from /api/sender/, so WifiLinkPanel
     * renders only the live one - whether it is desynced right NOW, which is the part that
     * explains a strong signal sitting above failing uploads.
     */
    const desyncEpisodes = ref(0);
    const desyncCurrentSec = ref(null);
    const desyncLongestSec = ref(0);
    const desyncTotalSec = ref(0);

    const ssid = ref(null);
    const channel = ref(null);
    const ip = ref(null);

    /*
     * Which access point we are attached to, and how many times that has changed.
     *
     * A repeater serves the same SSID as the router's own access point, so on a repeated
     * network `ssid` cannot distinguish them and RSSI to a nearby repeater reads excellent
     * regardless of how bad its backhaul is. The BSSID is what identifies the radio, and a
     * climbing roam count is what flapping between two of them looks like.
     */
    const bssid = ref(null);
    const roams = ref(0);

    // Re-associations the firmware forced because the link was parked on a poor access
    // point. Kept apart from `roams` and `outages`, neither of which counts a drop the
    // device caused deliberately.
    const roamRecoveries = ref(0);

    // Attempts and the last outcome, so a recovery that ran and failed is visible. Counting
    // only successes once hid a broken association path for hours.
    const roamAttempts = ref(0);
    const roamLastResult = ref("NONE");
    const roamLastAttemptAgoSec = ref(null);

    // Signal. rssiLatest is live; the aggregates describe a rolling window (windowSec long).
    const rssiLatest = ref(null);
    const rssiQuality = ref(null);
    const rssiAverage = ref(null);
    const rssiMinimum = ref(null);
    const rssiMaximum = ref(null);
    const rssiSamples = ref(0);
    const windowSec = ref(0);

    // Stability - the half of "is the WiFi good?" that signal strength cannot answer.
    const outages = ref(0);
    const connectedForSec = ref(null);
    const currentOutageSec = ref(null);
    const lastOutageAgoSec = ref(null);
    const lastOutageDurationSec = ref(null);

    const loaded = ref(false);
    const wifiLinkError = ref(false);

    /**
     * Adopt a payload from either endpoint. `?? null` throughout rather than `||`, because
     * a genuine RSSI of 0 dBm and a channel of 0 are falsy but meaningful, and losing them
     * to a truthiness test would silently blank the indicator.
     */
    function applyPayload(w) {
        if (!w) {
            wifiLinkError.value = true;
            return;
        }

        associated.value = !!w.associated;
        managerConnected.value = !!w.managerConnected;
        desyncEpisodes.value = w.desyncEpisodes ?? 0;
        desyncCurrentSec.value = w.desyncCurrentSec ?? null;
        desyncLongestSec.value = w.desyncLongestSec ?? 0;
        desyncTotalSec.value = w.desyncTotalSec ?? 0;

        ssid.value = w.ssid ?? null;
        channel.value = w.channel ?? null;
        ip.value = w.ip ?? null;
        bssid.value = w.bssid ?? null;
        roams.value = w.roams ?? 0;
        roamRecoveries.value = w.roamRecoveries ?? 0;
        roamAttempts.value = w.roamAttempts ?? 0;
        roamLastResult.value = w.roamLastResult ?? "NONE";
        roamLastAttemptAgoSec.value = w.roamLastAttemptAgoSec ?? null;

        rssiLatest.value = w.rssiLatest ?? null;
        rssiQuality.value = w.rssiQuality ?? null;
        rssiAverage.value = w.rssiAverage ?? null;
        rssiMinimum.value = w.rssiMinimum ?? null;
        rssiMaximum.value = w.rssiMaximum ?? null;
        rssiSamples.value = w.rssiSamples ?? 0;
        windowSec.value = w.windowSec ?? 0;

        outages.value = w.outages ?? 0;
        connectedForSec.value = w.connectedForSec ?? null;
        currentOutageSec.value = w.currentOutageSec ?? null;
        lastOutageAgoSec.value = w.lastOutageAgoSec ?? null;
        lastOutageDurationSec.value = w.lastOutageDurationSec ?? null;

        loaded.value = true;
        wifiLinkError.value = false;
    }

    async function getWifiLink() {
        try {
            const remote_api = mande("/api/network/", genCSRFOptions());
            const response = await remote_api.get();
            applyPayload(response);
        } catch (error) {
            wifiLinkError.value = true;
        }
    }

    /**
     * The firmware's manager flag reporting down while the interface is demonstrably
     * associated. Its own counter is what proves the desynchronisation happened at all, so
     * a non-zero count is worth surfacing even once the two agree again.
     */
    function wifiDesynced() {
        return associated.value && !managerConnected.value;
    }

    return {
        associated, managerConnected,
        desyncEpisodes, desyncCurrentSec, desyncLongestSec, desyncTotalSec,
        ssid, channel, ip, bssid, roams, roamRecoveries,
        roamAttempts, roamLastResult, roamLastAttemptAgoSec,
        rssiLatest, rssiQuality, rssiAverage, rssiMinimum, rssiMaximum, rssiSamples, windowSec,
        outages, connectedForSec, currentOutageSec, lastOutageAgoSec, lastOutageDurationSec,
        loaded, wifiLinkError,
        applyPayload, getWifiLink, wifiDesynced,
    };
});
