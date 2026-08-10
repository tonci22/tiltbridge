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

      <!-- Same-colour Tilts are merged when combineTilts is on, so there is no physical
           device to attach per-device settings to. -->
      <div class="bg-white px-4 pt-4 sm:px-6" v-if="configStore.combineTilts">
        <div class="rounded-md bg-yellow-50 p-4">
          <div class="flex">
            <div class="flex-shrink-0">
              <ExclamationTriangleIcon class="h-5 w-5 text-yellow-400" aria-hidden="true" />
            </div>
            <div class="ml-3 text-sm text-yellow-800">
              {{ $t('tilt_list.combine_tilts_notice') }}
            </div>
          </div>
        </div>
      </div>

      <div class="bg-white px-2 py-5 sm:px-6">
        <table class="min-w-full divide-y divide-gray-200">
          <thead class="bg-gray-50">
          <tr>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.name_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden md:table-cell">{{ $t('tilt_list.color_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.mac_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.model_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.rssi_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden md:table-cell">{{ $t('tilt_list.battery_age_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.gravity_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_list.temperature_column') }}</th>
            <th scope="col" class="px-3 sm:px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('tilt_list.last_received_column') }}</th>
          </tr>
          </thead>
          <tbody>
          <!-- Keyed on the device id: colour is not unique once two same-colour Tilts are in
               range, and duplicate keys make Vue reuse the wrong row. -->
          <tr v-for="(tilt, tiltIdx) in tiltStore.tilts" :key="tilt.rowKey" :class="tiltIdx % 2 === 0 ? 'bg-white' : 'bg-gray-50'">
            <td class="relative px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
              <div class="absolute inset-y-0 left-0 w-1.5 bg-indigo-600" :style="tilt.colorStyle"></div>
              <div class="flex items-center gap-2">
                <span :class="tilt.enabled ? '' : 'text-gray-400 line-through'">{{ tilt.friendlyName }}</span>

                <button @click="router.push(`/calibrate/${tilt.calibrationTarget}/`)" class="text-gray-500 hover:text-gray-700" :title="$t('tilt_list.calibrate_button')">
                  <AdjustmentsHorizontalIcon class="h-4 w-4" aria-hidden="true" />
                </button>

                <button
                    @click="openDeviceConfig(tilt)"
                    :disabled="configStore.combineTilts"
                    :class="[tilt.hasDeviceConfig ? 'text-indigo-600 hover:text-indigo-800' : 'text-gray-400 hover:text-gray-600', configStore.combineTilts ? 'opacity-40 cursor-not-allowed' : '']"
                    :title="tilt.hasDeviceConfig ? $t('tilt_list.configure_button') : $t('tilt_list.configure_button_unconfigured')">
                  <!-- Solid gear = this Tilt has its own configuration; outline gear = still
                       falling back to the shared colour settings. -->
                  <Cog6ToothIconSolid v-if="tilt.hasDeviceConfig" class="h-4 w-4" aria-hidden="true" />
                  <Cog6ToothIcon v-else class="h-4 w-4" aria-hidden="true" />
                </button>

                <span v-if="tilt.gsheets_name.length > 0">
                  <a :href="tilt.gsheets_link"><DocumentChartBarIcon class="h-4 w-4 text-green-700 inline" aria-hidden="true" /></a>
                </span>
              </div>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden md:table-cell">
              <span v-if="tilt.color === 'Red'">{{ $t('sitewide.tilt_colors.red') }}</span>
              <span v-else-if="tilt.color === 'Green'">{{ $t('sitewide.tilt_colors.green') }}</span>
              <span v-else-if="tilt.color === 'Black'">{{ $t('sitewide.tilt_colors.black') }}</span>
              <span v-else-if="tilt.color === 'Purple'">{{ $t('sitewide.tilt_colors.purple') }}</span>
              <span v-else-if="tilt.color === 'Orange'">{{ $t('sitewide.tilt_colors.orange') }}</span>
              <span v-else-if="tilt.color === 'Blue'">{{ $t('sitewide.tilt_colors.blue') }}</span>
              <span v-else-if="tilt.color === 'Yellow'">{{ $t('sitewide.tilt_colors.yellow') }}</span>
              <span v-else-if="tilt.color === 'Pink'">{{ $t('sitewide.tilt_colors.pink') }}</span>
              <span v-else>{{ tilt.color }}</span>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-xs font-mono text-gray-700 hidden lg:table-cell">{{ tilt.deviceId }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden lg:table-cell">{{ tilt.modelLabel }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium hidden lg:table-cell" :title="rssiTooltip(tilt)">
              <span :class="tilt.rssiQualityClass">{{ $t('tilt_list.rssi_value', { dbm: tilt.rssiLatest, quality: tilt.rssiQuality }) }}</span>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden md:table-cell">
              <span v-if="tilt.sends_battery">{{ $t('tilt_list.battery_age_week_count', { weeks: tilt.weeks_on_battery}) }}</span>
              <span v-else>--</span>
            </td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ tilt.formattedGravity(tiltStore.gravityUnit) }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ tilt.temp }}&deg; {{ tilt.temp_unit }}</td>
            <td class="px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 hidden lg:table-cell">{{ $t('tilt_list.last_received_ago', { seconds: tilt.lastReceived }) }}</td>
          </tr>

          <tr v-if="tiltStore.tilts.length === 0">
            <td class="relative px-3 sm:px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900" colspan="9">
              <div class="absolute inset-y-0 left-0 w-1.5 bg-red-500"></div>
              <ExclamationTriangleIcon class="h-5 w-5 inline text-red-500" /> {{ $t('tilt_list.no_tilts_detected') }}
            </td>
          </tr>

          </tbody>
        </table>
      </div>

    </main>

    <DeviceConfigModal v-model="deviceConfigOpen" :tilt="deviceConfigTilt" @saved="onDeviceConfigSaved" />
    <UpdateSuccessfulModal v-model="alertOpen" :update-successful="updateSuccessful" />
  </div>
</template>

<script setup>
import { useTiltStore } from "@/stores/TiltStore";
import { useSendTargetErrorStore } from "@/stores/SendTargetErrorStore";
import { useConfigStore } from "@/stores/ConfigStore";
import SendTargetErrorBanner from "@/components/generic/SendTargetErrorBanner.vue";
import DeviceConfigModal from "@/components/config/TiltBridge/DeviceConfigModal.vue";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";
import { DocumentChartBarIcon, ExclamationTriangleIcon, AdjustmentsHorizontalIcon, Cog6ToothIcon } from "@heroicons/vue/24/outline";
import { Cog6ToothIcon as Cog6ToothIconSolid } from "@heroicons/vue/20/solid";
import { onMounted, onBeforeUnmount, ref } from "vue";
import { useRouter } from "vue-router";
import { i18n } from "@/main.js";

let intervalObject = null;
const tiltStore = useTiltStore();
const errorStore = useSendTargetErrorStore();
const configStore = useConfigStore();
const router = useRouter();

const deviceConfigOpen = ref(false);
const deviceConfigTilt = ref(null);
const alertOpen = ref(false);
const updateSuccessful = ref(false);

/**
 * The quality bands themselves are firmware-side (src/rssi_stats.h); this only surfaces the
 * aggregates behind the value.
 */
function rssiTooltip(tilt) {
  return i18n.global.t('tilt_list.rssi_tooltip', {
    average: tilt.rssiAverage ?? '--',
    minimum: tilt.rssiMinimum ?? '--',
    maximum: tilt.rssiMaximum ?? '--',
    samples: tilt.rssiSamples ?? 0,
  });
}

function openDeviceConfig(tilt) {
  if (configStore.combineTilts) return;
  deviceConfigTilt.value = tilt;
  deviceConfigOpen.value = true;
}

async function onDeviceConfigSaved(success) {
  updateSuccessful.value = success;
  alertOpen.value = true;
  await tiltStore.getTilts();
}

onMounted(() => {
  // Retrieve initial data
  tiltStore.getTilts();
  errorStore.getErrors();
  // combineTilts drives the notice and the gear buttons. Only pull if nothing else has.
  if (!configStore.loaded) {
    configStore.getConfig();
  }

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
