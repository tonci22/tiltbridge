import { defineStore } from 'pinia';
import { mande } from 'mande'
import { genCSRFOptions } from './CSRF';
import { ref } from 'vue';

export const TARGET_META = {
    legacy_fermentrack: { routeName: 'LegacyFermentrackConfig' },
    fermentrack:        { routeName: 'FermentrackConfig' },
    brewers_friend:     { routeName: 'BrewersFriendConfig' },
    brewfather:         { routeName: 'BrewfatherConfig' },
    user_target:        { routeName: 'GenericTargetConfig' },
    grainfather:        { routeName: 'GrainfatherConfig' },
    brew_status:        { routeName: 'BrewstatusConfig' },
    taplistio:          { routeName: 'TaplistIOConfig' },
    google_sheets:      { routeName: 'GoogleSheetsConfig' },
    mqtt:               { routeName: 'MQTTConfig' },
    influxdb:           { routeName: 'InfluxDBConfig' },
};

export const useSendTargetErrorStore = defineStore("SendTargetErrorStore", () => {
    const targets = ref({});
    const loaded = ref(false);
    const fetchError = ref(false);

    async function getErrors() {
        try {
            const remote_api = mande("/api/errors/", genCSRFOptions());
            const response = await remote_api.get();
            if (response) {
                targets.value = response;
                loaded.value = true;
                fetchError.value = false;
            } else {
                await clearErrors();
                fetchError.value = true;
            }
        } catch (error) {
            await clearErrors();
            fetchError.value = true;
        }
    }

    async function clearErrors() {
        targets.value = {};
        loaded.value = false;
    }

    function hasErrors() {
        return Object.values(targets.value).some(
            (target) => target.error_code !== 0
        );
    }

    function getTargetError(targetKey) {
        return targets.value[targetKey] || null;
    }

    function isTargetError(targetKey) {
        const target = targets.value[targetKey];
        if (!target) return false;
        return target.error_code !== 0;
    }

    return { targets, loaded, fetchError, getErrors, clearErrors, hasErrors, getTargetError, isTargetError };
});
