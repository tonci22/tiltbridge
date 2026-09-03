<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('queue.status.header') }}
          </h3>
          <p class="mt-1 max-w-2xl text-sm text-gray-500">
            {{ $t('queue.status.description') }}
          </p>
        </div>

        <!-- Readings were thrown away because the queue filled up. Never quiet about this. -->
        <div class="px-4 pt-4 sm:px-6" v-if="queueStore.droppedOverflow > 0">
          <div class="rounded-md bg-red-50 p-4 border border-red-300">
            <div class="flex">
              <div class="flex-shrink-0">
                <ExclamationTriangleIcon class="h-5 w-5 text-red-500" aria-hidden="true" />
              </div>
              <div class="ml-3">
                <h3 class="text-sm font-bold text-red-800">
                  {{ $t('queue.status.dropped_warning_title', { count: queueStore.droppedOverflow }) }}
                </h3>
                <p class="mt-1 text-sm text-red-700">
                  {{ $t('queue.status.dropped_warning_desc') }}
                </p>
              </div>
            </div>
          </div>
        </div>

        <!-- Queued readings will be uploaded with TimestampValid: false until NTP succeeds. -->
        <div class="px-4 pt-4 sm:px-6" v-if="queueStore.loaded && !queueStore.timeValid">
          <div class="rounded-md bg-yellow-50 p-4">
            <div class="flex">
              <div class="flex-shrink-0">
                <ClockIcon class="h-5 w-5 text-yellow-500" aria-hidden="true" />
              </div>
              <div class="ml-3">
                <h3 class="text-sm font-medium text-yellow-800">
                  {{ $t('queue.status.time_not_valid_title') }}
                </h3>
                <p class="mt-1 text-sm text-yellow-700">
                  {{ $t('queue.status.time_not_valid_desc') }}
                </p>
              </div>
            </div>
          </div>
        </div>

        <div class="flex flex-col">
          <div class="-my-2 overflow-x-auto sm:-mx-6 lg:-mx-8">
            <div class="py-2 align-middle min-w-full sm:px-6 lg:px-8">
              <div class="shadow overflow-hidden border-b border-gray-200 sm:rounded-lg">
                <table class="min-w-full divide-y divide-gray-200">
                  <tbody class="bg-white divide-y divide-gray-200">

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.queued_readings') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ queueStore.queuedReadings }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.oldest_reading') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="oldestAge">{{ $t('sitewide.age_ago', { age: oldestAge }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.none') }}</span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.storage_used') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 text-sm font-medium text-gray-900">
                      <div class="flex items-center gap-3">
                        <div class="w-24 sm:w-40 bg-gray-200 rounded-full h-2.5">
                          <div class="h-2.5 rounded-full" :class="storageBarClass" :style="storageBarStyle"></div>
                        </div>
                        <span>{{ $t('queue.status.storage_percent', { percent: storagePercent }) }}</span>
                      </div>
                      <div class="mt-1 text-xs text-gray-500">
                        {{ $t('queue.status.storage_bytes', { bytes: queueStore.bytesUsed, record: queueStore.recordSize }) }}
                      </div>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.maximum_records') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ queueStore.maxRecords }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.snapshot_interval') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ $t('queue.status.minutes', { minutes: snapshotIntervalMinutes }) }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.last_upload') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="lastUploadAge">{{ $t('sitewide.age_ago', { age: lastUploadAge }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.never') }}</span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.upload_status') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium" :class="uploadStatusClass">
                      {{ uploadStatusLabel }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.dropped_overflow') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm" :class="queueStore.droppedOverflow > 0 ? 'font-bold text-red-600' : 'font-medium text-gray-900'">
                      <ExclamationTriangleIcon v-if="queueStore.droppedOverflow > 0" class="h-4 w-4 inline mr-1 text-red-600" aria-hidden="true" />
                      {{ queueStore.droppedOverflow }}
                    </td>
                  </tr>

                  <tr v-if="!queueStore.enabled">
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.queue_state') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-medium text-gray-500">
                      {{ $t('queue.status.queue_disabled') }}
                    </td>
                  </tr>

                  <tr v-if="queueStore.loaded && !queueStore.healthy">
                    <th scope="row" class="px-3 py-3 sm:px-6 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('queue.status.health') }}
                    </th>
                    <td class="px-3 py-4 sm:px-6 md:whitespace-nowrap text-sm font-bold text-red-600">
                      {{ $t('queue.status.unhealthy') }}
                    </td>
                  </tr>

                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>

        <div class="px-4 py-5 sm:px-6 border-t border-gray-200 mt-6">
          <div class="flex flex-wrap gap-3">
            <button
                type="button"
                class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-blue-600 hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500"
                :disabled="queueStore.queuedReadings === 0 || queueStore.backlogRequested"
                @click="sendBacklog">
              {{ queueStore.backlogRequested ? $t('queue.status.send_backlog_pending') : $t('queue.status.send_backlog_button') }}
            </button>
            <button
                type="button"
                class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-red-600 hover:bg-red-700 disabled:bg-gray-400 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-red-500"
                :disabled="queueStore.queuedReadings === 0"
                @click="clearModalOpen = true">
              {{ $t('queue.status.clear_queue_button') }}
            </button>
          </div>

          <!-- The firmware only sets a flag; send_to_google_v2() cannot act on it until it can
               take the sender lock, and an Apps Script round trip is tens of seconds. Saying so
               is the difference between "waiting" and "broken". -->
          <p class="mt-3 text-sm text-gray-600" v-if="queueStore.backlogRequested">
            {{ $t('queue.status.send_backlog_pending_desc') }}
          </p>

          <p class="mt-3 text-sm text-red-700" v-if="queueStore.queueActionError">
            {{ $t('queue.status.action_failed') }}
          </p>
          <p class="mt-3 text-sm text-red-700" v-else-if="queueStore.queueError">
            {{ $t('queue.status.load_failed') }}
          </p>
        </div>

      </div>
    </div>

    <ClearQueueModal v-model="clearModalOpen" :queued-readings="queueStore.queuedReadings" @confirmed="clearQueue" />
  </div>
</template>

<script setup>
import { computed, ref } from 'vue';
import { ExclamationTriangleIcon, ClockIcon } from "@heroicons/vue/24/outline";
import ClearQueueModal from "@/components/config/TiltBridge/ClearQueueModal.vue";
import { useQueueStore } from "@/stores/QueueStore";
import { formatAge } from "@/mixins/FormatAge";
import { i18n } from "@/main.js";

const queueStore = useQueueStore();
const clearModalOpen = ref(false);

const storagePercent = computed(() => {
  const pct = Number(queueStore.storagePercent) || 0;
  return Math.min(100, Math.max(0, Math.round(pct)));
});

const storageBarStyle = computed(() => `width: ${storagePercent.value}%;`);

const storageBarClass = computed(() => {
  if (storagePercent.value > 90) return 'bg-red-600';
  if (storagePercent.value > 75) return 'bg-amber-500';
  return 'bg-green-600';
});

const snapshotIntervalMinutes = computed(() => Math.round((queueStore.snapshotIntervalSec || 0) / 60));

const oldestAge = computed(() => (queueStore.queuedReadings > 0 ? formatAge(queueStore.oldestReadingAgeSec) : null));
const lastUploadAge = computed(() => formatAge(queueStore.lastUploadSuccessAgeSec));

const uploadStatusLabel = computed(() => {
  const key = `queue.upload_status.${(queueStore.uploadStatus || 'IDLE').toLowerCase()}`;
  return i18n.global.te(key) ? i18n.global.t(key) : queueStore.uploadStatus;
});

const uploadStatusClass = computed(() => {
  switch (queueStore.uploadStatus) {
    case 'SENDING': return 'text-blue-600';
    case 'RETRYING': return 'text-amber-600';
    case 'DISABLED': return 'text-gray-500';
    default: return 'text-gray-900';
  }
});

async function sendBacklog() {
  await queueStore.sendBacklogNow();
}

async function clearQueue() {
  await queueStore.clearQueue();
}
</script>

<style scoped>

</style>
