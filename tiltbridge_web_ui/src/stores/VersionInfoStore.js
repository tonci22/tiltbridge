// This version of VersionInfoStore.js is defined as a "setup" store.
import { defineStore } from 'pinia';
import { mande } from 'mande'
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";

export const useVersionInfoStore = defineStore("VersionInfoStore", () => {
    const hasVersionInfo = ref(false);
    const versionInfoError = ref(false);
    const versionNumber = ref("");
    const branchName = ref("");
    const buildChecksum = ref("");

    async function getVersionInfo() {
        try {
            const remote_api = mande("/api/version/", genCSRFOptions());
            const response = await remote_api.get();
            if (response && response.version) {
                hasVersionInfo.value = true;
                versionInfoError.value = false;
                versionNumber.value = response.version;
                branchName.value = response.branch;
                buildChecksum.value = response.build;
            } else {
                await clearVersionInfo();
                versionInfoError.value = true;
            }
        } catch (error) {
            await clearVersionInfo();
            versionInfoError.value = true;
        }
    }

    async function clearVersionInfo() {
        hasVersionInfo.value = false;
        versionNumber.value = "";
        branchName.value = "";
        buildChecksum.value = "";
    }

    return {
        hasVersionInfo,
        versionInfoError,
        versionNumber,
        branchName,
        buildChecksum,

        getVersionInfo,
        clearVersionInfo,
    };
});
