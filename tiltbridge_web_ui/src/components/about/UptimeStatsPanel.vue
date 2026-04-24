<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t("about.uptime.version_and_uptime") }}
          </h3>
        </div>

        <div class="flex flex-col">
          <div class="-my-2 overflow-x-auto sm:-mx-6 lg:-mx-8">
            <div class="py-2 align-middle inline-block min-w-full sm:px-6 lg:px-8">
              <div class="shadow overflow-hidden border-b border-gray-200 sm:rounded-lg">
                <table class="min-w-full divide-y divide-gray-200">
                  <tbody class="bg-white divide-y divide-gray-200">
                  <tr>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t("about.uptime.firmware_version") }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="VersionInfoStore.hasVersionInfo">TiltBridge v{{ VersionInfoStore.versionNumber }}[{{ VersionInfoStore.branchName }}] ({{ VersionInfoStore.buildChecksum }})</span>
                      <span v-else-if="VersionInfoStore.versionInfoError">{{ $t('about.version.loading_version')}}</span>
                      <span v-else>{{ $t('about.version.error_loading_version')}}</span>

                    </td>
                  </tr>
                  <tr>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t("about.uptime.uptime_header") }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ $t('about.uptime.uptime_print', { days: UptimeStatsStore.days, hours: UptimeStatsStore.hours, minutes: UptimeStatsStore.minutes, seconds: UptimeStatsStore.seconds}) }}
                    </td>
                  </tr>
                  <tr>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t("about.uptime.reset_header") }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ $t('about.uptime.reset_print', { reason: ResetReasonStore.reason, description: ResetReasonStore.description}) }}
                    </td>
                  </tr>
                  <tr v-if="HeapInfoStore.hasHeapInfo">
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t("about.uptime.heap_header") }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ $t('about.uptime.heap_print', { free_heap: HeapInfoStore.free, max_heap: HeapInfoStore.max, frags: HeapInfoStore.frag }) }}
                    </td>
                  </tr>

                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>


      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue';
import { useUptimeStatsStore } from "@/stores/UptimeStatsStore";
import { useVersionInfoStore } from "@/stores/VersionInfoStore.js";
import { useHeapInfoStore } from "@/stores/HeapInfoStore";
import { useResetReasonStore } from "@/stores/ResetReasonStore";

const UptimeStatsStore = useUptimeStatsStore();
const VersionInfoStore = useVersionInfoStore();
const HeapInfoStore = useHeapInfoStore();
const ResetReasonStore = useResetReasonStore();

const statsIntervalObject = ref(null);
const heapIntervalObject = ref(null);
const resetIntervalObject = ref(null);

onMounted(() => {
  // Retrieve initial data
  UptimeStatsStore.getUptimeStats();
  VersionInfoStore.getVersionInfo();
  HeapInfoStore.getHeapInfo();
  ResetReasonStore.getResetReason();

  // Set up periodic refreshes
  statsIntervalObject.value = window.setInterval(() => {
    UptimeStatsStore.getUptimeStats();
  }, 10000);
  
  heapIntervalObject.value = window.setInterval(() => {
    HeapInfoStore.getHeapInfo();
  }, 9000);
  
  resetIntervalObject.value = window.setInterval(() => {
    ResetReasonStore.getResetReason();
  }, 60000);
});

onBeforeUnmount(() => {
  clearInterval(statsIntervalObject.value);
  clearInterval(heapIntervalObject.value);
  clearInterval(resetIntervalObject.value);
});
</script>

<style scoped>

</style>