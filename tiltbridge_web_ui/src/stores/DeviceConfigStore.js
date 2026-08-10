import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";

/**
 * Per-physical-device (MAC) configuration, wrapping /api/devices/.
 *
 * Everything here is keyed on the canonical device id rather than the Tilt colour, which is
 * the whole point of the endpoint: two Tilts of the same colour need separate settings.
 */
export const useDeviceConfigStore = defineStore("DeviceConfigStore", () => {

    const devices = ref([]);
    const schemaVersion = ref(0);
    const maxDevices = ref(0);
    const loaded = ref(false);
    const devicesError = ref(false);
    const deviceUpdateError = ref(false);

    async function getDevices() {
        try {
            const remote_api = mande("/api/devices/", genCSRFOptions());
            const response = await remote_api.get();
            if (response) {
                devices.value = response.devices || [];
                schemaVersion.value = response.schemaVersion || 0;
                maxDevices.value = response.maxDevices || 0;
                loaded.value = true;
                devicesError.value = false;
            } else {
                devicesError.value = true;
            }
        } catch (error) {
            devicesError.value = true;
        }
    }

    /**
     * The firmware canonicalises device ids to uppercase, but be forgiving about case here so
     * a lookup never silently misses.
     */
    function findDevice(deviceId) {
        if (!deviceId) return undefined;
        const needle = String(deviceId).toLowerCase();
        return devices.value.find((d) => {
            if (d.deviceId && d.deviceId.toLowerCase() === needle) return true;
            return !!d.mac && d.mac.toLowerCase() === needle;
        });
    }

    /**
     * Upsert. The firmware creates the entry when it does not already exist, so this is also
     * how a newly detected Tilt gets its first configuration.
     */
    async function saveDevice(payload) {
        try {
            const remote_api = mande("/api/devices/", genCSRFOptions());
            const response = await remote_api.put(payload);
            if (response && response.status === "ok") {
                deviceUpdateError.value = false;
                await getDevices();
                return true;
            }
            deviceUpdateError.value = true;
            return false;
        } catch (error) {
            deviceUpdateError.value = true;
            return false;
        }
    }

    /**
     * Drops the device record so the Tilt falls back to the shared colour configuration.
     */
    async function resetDeviceToColorDefaults(deviceId) {
        try {
            const remote_api = mande("/api/devices/delete/", genCSRFOptions());
            const response = await remote_api.post({ deviceId: deviceId });
            if (response && response.status === "ok") {
                deviceUpdateError.value = false;
                await getDevices();
                return true;
            }
            deviceUpdateError.value = true;
            return false;
        } catch (error) {
            deviceUpdateError.value = true;
            return false;
        }
    }

    function clearDevices() {
        devices.value = [];
        schemaVersion.value = 0;
        maxDevices.value = 0;
        loaded.value = false;
        devicesError.value = false;
        deviceUpdateError.value = false;
    }

    return {
        devices,
        schemaVersion,
        maxDevices,
        loaded,
        devicesError,
        deviceUpdateError,

        getDevices,
        findDevice,
        saveDevice,
        resetDeviceToColorDefaults,
        clearDevices,
    };
});
