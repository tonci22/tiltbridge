import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";

/**
 * Outbound sender health (GET /api/sender/).
 *
 * Polled faster than the queue because this is the panel a user watches when the device is
 * misbehaving. `lastRecovery` is the payoff field: it proves the stale-sender watchdog fired.
 */
export const useSenderHealthStore = defineStore("SenderHealthStore", () => {

    const state = ref("IDLE");
    const currentTarget = ref(null);
    const heartbeatAgeSec = ref(0);
    const lockHeld = ref(false);
    const lockAgeSec = ref(0);
    const requestAgeSec = ref(0);
    const lastGoogleSuccessAgeSec = ref(null);
    const lastFermentrackSuccessAgeSec = ref(null);
    const lastAnySuccessAgeSec = ref(null);
    const consecutiveSendFailures = ref(0);
    const staleEvents = ref(0);
    // Episodes, not polls: the firmware used to report a counter that ticked ~100x a second
    // while the WiFi manager's flag was stale, so one minute read as thousands.
    const wifiDesyncEpisodes = ref(0);
    const wifiDesyncLongestSec = ref(0);
    const lastRecovery = ref(null);

    const loaded = ref(false);
    const senderError = ref(false);

    async function getSenderHealth() {
        try {
            const remote_api = mande("/api/sender/", genCSRFOptions());
            const response = await remote_api.get();
            if (response) {
                state.value = response.state || "IDLE";
                currentTarget.value = response.currentTarget ?? null;
                heartbeatAgeSec.value = response.heartbeatAgeSec ?? 0;
                lockHeld.value = response.lockHeld ?? false;
                lockAgeSec.value = response.lockAgeSec ?? 0;
                requestAgeSec.value = response.requestAgeSec ?? 0;
                lastGoogleSuccessAgeSec.value = response.lastGoogleSuccessAgeSec ?? null;
                lastFermentrackSuccessAgeSec.value = response.lastFermentrackSuccessAgeSec ?? null;
                lastAnySuccessAgeSec.value = response.lastAnySuccessAgeSec ?? null;
                consecutiveSendFailures.value = response.consecutiveSendFailures ?? 0;
                staleEvents.value = response.staleEvents ?? 0;
                wifiDesyncEpisodes.value = response.wifiDesyncEpisodes ?? 0;
                wifiDesyncLongestSec.value = response.wifiDesyncLongestSec ?? 0;
                lastRecovery.value = response.lastRecovery ?? null;

                loaded.value = true;
                senderError.value = false;
            } else {
                senderError.value = true;
            }
        } catch (error) {
            senderError.value = true;
        }
    }

    function clearSenderHealth() {
        state.value = "IDLE";
        currentTarget.value = null;
        heartbeatAgeSec.value = 0;
        lockHeld.value = false;
        lockAgeSec.value = 0;
        requestAgeSec.value = 0;
        lastGoogleSuccessAgeSec.value = null;
        lastFermentrackSuccessAgeSec.value = null;
        lastAnySuccessAgeSec.value = null;
        consecutiveSendFailures.value = 0;
        staleEvents.value = 0;
        wifiDesyncEpisodes.value = 0;
        wifiDesyncLongestSec.value = 0;
        lastRecovery.value = null;
        loaded.value = false;
    }

    return {
        state,
        currentTarget,
        heartbeatAgeSec,
        lockHeld,
        lockAgeSec,
        requestAgeSec,
        lastGoogleSuccessAgeSec,
        lastFermentrackSuccessAgeSec,
        lastAnySuccessAgeSec,
        consecutiveSendFailures,
        staleEvents,
        wifiDesyncEpisodes,
        wifiDesyncLongestSec,
        lastRecovery,
        loaded,
        senderError,

        getSenderHealth,
        clearSenderHealth,
    };
});
