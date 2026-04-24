<template>
  <div>
    <header class="bg-white shadow">
      <div class="max-w-7xl mx-auto py-6 px-4 sm:px-6 lg:px-8">
        <h1 class="text-3xl font-bold text-gray-900">
          {{ $t('tilt_list.header') }}
        </h1>
      </div>
    </header>
    <main>

      <div class="px-4 sm:px-6 lg:px-8 py-4" v-if="errorStore.loaded && errorStore.hasErrors()">
        <SendTargetErrorBanner />
      </div>

      <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
        <h3 class="text-lg leading-6 font-medium text-gray-900">
          {{ $t('tilt_list.detected_tilts') }}
        </h3>
      </div>

      <div class="bg-white px-2 py-5 sm:px-6">
        <table class="min-w-full divide-y divide-gray-200">
          <thead class="bg-gray-50">
          <tr>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.color_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.rssi_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden md:table-cell">{{ $t('tilt_list.battery_age_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.gravity_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.temperature_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.last_received_column') }}</th>

  <!--          <th scope="col" class="relative px-6 py-3"><span class="sr-only">Actions</span></th>-->
          </tr>
          </thead>
          <tbody>
          <tr v-for="(tilt, tiltIdx) in tiltStore.tilts" :key="tilt.color" :class="tiltIdx % 2 === 0 ? 'bg-white' : 'bg-gray-50'">
            <td class="relative px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
              <div class="absolute inset-y-0 left-0 w-1.5 bg-indigo-600" :style="tilt.colorStyle"></div>
              <div class="flex items-center gap-2">
                <span>
                  <span v-if="tilt.color === 'Red'">{{ $t('sitewide.tilt_colors.red') }}</span>
                  <span v-else-if="tilt.color === 'Green'">{{ $t('sitewide.tilt_colors.green') }}</span>
                  <span v-else-if="tilt.color === 'Black'">{{ $t('sitewide.tilt_colors.black') }}</span>
                  <span v-else-if="tilt.color === 'Purple'">{{ $t('sitewide.tilt_colors.purple') }}</span>
                  <span v-else-if="tilt.color === 'Orange'">{{ $t('sitewide.tilt_colors.orange') }}</span>
                  <span v-else-if="tilt.color === 'Blue'">{{ $t('sitewide.tilt_colors.blue') }}</span>
                  <span v-else-if="tilt.color === 'Yellow'">{{ $t('sitewide.tilt_colors.yellow') }}</span>
                  <span v-else-if="tilt.color === 'Pink'">{{ $t('sitewide.tilt_colors.pink') }}</span>
                </span>
                <button @click="router.push(`/calibrate/${tilt.color.toLowerCase()}`)" class="text-gray-500 hover:text-gray-700">
                  <AdjustmentsHorizontalIcon class="h-4 w-4" aria-hidden="true" />
                </button>
                <span v-if="tilt.gsheets_name.length > 0">
                  <!-- TODO - Fix the link below -->
                  <a :href="tilt.gsheets_link"><DocumentChartBarIcon class="h-4 w-4 text-green-700 inline" aria-hidden="true" /></a>
                </span>
              </div>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden lg:table-cell">{{ tilt.rssi }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden md:table-cell">
              <span v-if="tilt.sends_battery">{{ $t('tilt_list.battery_age_week_count', { weeks: tilt.weeks_on_battery}) }}</span>
              <span v-else>--</span>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ tilt.formattedGravity(tiltStore.gravityUnit) }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ tilt.temp }}&deg; {{ tilt.temp_unit }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden lg:table-cell">{{ $t('tilt_list.last_received_ago', { seconds: tilt.lastReceived }) }}</td>

  <!--          <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">-->
  <!--          </td>-->
          </tr>

          <tr v-if="tiltStore.tilts.length === 0">
            <td class="relative px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900" colspan="6">
              <div class="absolute inset-y-0 left-0 w-1.5 bg-red-500"></div>
              <ExclamationTriangleIcon class="h-5 w-5 inline text-red-500" /> {{ $t('tilt_list.no_tilts_detected') }}
            </td>
          </tr>

          </tbody>
        </table>
      </div>

    </main>
  </div>
</template>

<script setup>
import { useTiltStore } from "@/stores/TiltStore";
import { useSendTargetErrorStore } from "@/stores/SendTargetErrorStore";
import SendTargetErrorBanner from "@/components/generic/SendTargetErrorBanner.vue";
// import { ExclamationTriangleIcon } from "@heroicons/vue/20/solid";
import { DocumentChartBarIcon, ExclamationTriangleIcon, AdjustmentsHorizontalIcon } from "@heroicons/vue/24/outline";
import { onMounted, onBeforeUnmount } from "vue";
import { useRouter } from "vue-router";

let intervalObject = null;
const tiltStore = useTiltStore();
const errorStore = useSendTargetErrorStore();
const router = useRouter();

onMounted(() => {
  // Retrieve initial data
  tiltStore.getTilts();
  errorStore.getErrors();

  // Set up periodic refreshes
  intervalObject = window.setInterval(() => {
    tiltStore.getTilts();
    errorStore.getErrors();
  }, 15000)  // Tilts send data a bit faster than once per second, but we don't need to update that fast
});

onBeforeUnmount(() => {
  clearInterval(intervalObject);
});

</script>


<style scoped>

</style>