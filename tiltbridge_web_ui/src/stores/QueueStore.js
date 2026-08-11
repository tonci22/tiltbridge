import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";

/**
 * Offline reading queue status (GET /api/queue/), actions (POST /api/queue/actions/) and the
 * four queue settings, which are written through the existing controller settings endpoint.
 */
export const useQueueStore = defineStore("QueueStore", () => {

    // Status
    const queuedReadings = ref(0);
    const oldestReadingAgeSec = ref(null);
    const storagePercent = ref(0);
    const bytesUsed = ref(0);
    const droppedOverflow = ref(0);
    const timeValid = ref(true);
    const recordSize = ref(0);
    const healthy = ref(true);
    const fsFreeBytes = ref(null);
    const uploadStatus = ref("IDLE");
    const lastUploadSuccessAgeSec = ref(null);

    // Settings, as currently applied on the device
    const maxRecords = ref(1500);
    // What the flash can actually hold, and how long a total outage would take to fill it.
    // 0 / null mean the device could not work them out; fall back to the static ceiling.
    const maxRecordsSupported = ref(0);
    const activeTilts = ref(0);
    const estimatedRunwayHours = ref(null);
    const snapshotIntervalSec = ref(1800);
    const batchSize = ref(20);
    const enabled = ref(false);

    const loaded = ref(false);
    const queueError = ref(false);
    const queueActionError = ref(false);
    const queueUpdateError = ref(false);

    async function getQueueStatus() {
        try {
            const remote_api = mande("/api/queue/", genCSRFOptions());
            const response = await remote_api.get();
            if (response) {
                queuedReadings.value = response.queuedReadings ?? 0;
                oldestReadingAgeSec.value = response.oldestReadingAgeSec ?? null;
                storagePercent.value = response.storagePercent ?? 0;
                bytesUsed.value = response.bytesUsed ?? 0;
                maxRecords.value = response.maxRecords ?? 1500;
                maxRecordsSupported.value = response.maxRecordsSupported ?? 0;
                activeTilts.value = response.activeTilts ?? 0;
                estimatedRunwayHours.value = response.estimatedRunwayHours ?? null;
                snapshotIntervalSec.value = response.snapshotIntervalSec ?? 1800;
                droppedOverflow.value = response.droppedOverflow ?? 0;
                timeValid.value = response.timeValid ?? false;
                recordSize.value = response.recordSize ?? 0;
                healthy.value = response.healthy ?? true;
                enabled.value = response.enabled ?? false;
                batchSize.value = response.batchSize ?? 20;
                fsFreeBytes.value = response.fsFreeBytes ?? null;
                uploadStatus.value = response.uploadStatus ?? "IDLE";
                lastUploadSuccessAgeSec.value = response.lastUploadSuccessAgeSec ?? null;

                loaded.value = true;
                queueError.value = false;
            } else {
                queueError.value = true;
            }
        } catch (error) {
            queueError.value = true;
        }
    }

    async function postAction(payload) {
        try {
            const remote_api = mande("/api/queue/actions/", genCSRFOptions());
            const response = await remote_api.post(payload);
            if (response && response.status === "ok") {
                queueActionError.value = false;
                return true;
            }
            queueActionError.value = true;
            return false;
        } catch (error) {
            queueActionError.value = true;
            return false;
        }
    }

    async function sendBacklogNow() {
        // Optimistic: the firmware picks the request up on its next sender pass, so reflect
        // it immediately rather than leaving the panel looking inert until the next poll.
        uploadStatus.value = "SENDING";
        const ok = await postAction({ action: "sendBacklogNow" });
        if (!ok) {
            await getQueueStatus();
        }
        return ok;
    }

    /**
     * The firmware rejects this with a 400 unless confirm:true is present. The UI gates it
     * behind an explicit acknowledgement modal as well.
     */
    async function clearQueue() {
        const ok = await postAction({ action: "clearQueue", confirm: true });
        await getQueueStatus();
        return ok;
    }

    /**
     * Queue settings live on the controller settings endpoint. The API is in seconds; the UI
     * presents minutes and converts here at the boundary.
     */
    async function updateQueueSettings({ snapshotIntervalSeconds, maximumRecords, batch, queueEnabled }) {
        try {
            const remote_api = mande("/api/settings/controller/", genCSRFOptions());
            const response = await remote_api.put({
                queueSnapshotIntervalSec: snapshotIntervalSeconds,
                maxQueuedRecords: maximumRecords,
                queueBatchSize: batch,
                offlineQueueEnabled: queueEnabled,
            });
            if (response && response.status === "ok") {
                queueUpdateError.value = false;
                await getQueueStatus();
                return true;
            }
            queueUpdateError.value = true;
            return false;
        } catch (error) {
            queueUpdateError.value = true;
            return false;
        }
    }

    function clearQueueStore() {
        queuedReadings.value = 0;
        oldestReadingAgeSec.value = null;
        storagePercent.value = 0;
        bytesUsed.value = 0;
        droppedOverflow.value = 0;
        timeValid.value = true;
        recordSize.value = 0;
        healthy.value = true;
        fsFreeBytes.value = null;
        uploadStatus.value = "IDLE";
        lastUploadSuccessAgeSec.value = null;
        loaded.value = false;
    }

    return {
        queuedReadings,
        oldestReadingAgeSec,
        storagePercent,
        bytesUsed,
        droppedOverflow,
        timeValid,
        recordSize,
        healthy,
        fsFreeBytes,
        uploadStatus,
        lastUploadSuccessAgeSec,
        maxRecords,
        maxRecordsSupported,
        activeTilts,
        estimatedRunwayHours,
        snapshotIntervalSec,
        batchSize,
        enabled,
        loaded,
        queueError,
        queueActionError,
        queueUpdateError,

        getQueueStatus,
        sendBacklogNow,
        clearQueue,
        updateQueueSettings,
        clearQueueStore,
    };
});
