<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('wifi.panel_header') }}
          </h3>
        </div>

        <!-- The manager believing it is disconnected while the interface is associated is
             what silently disables every outbound target, so it gets a banner rather than a
             row: a healthy signal sitting above failing uploads is otherwise baffling. -->
        <div class="px-4 pt-4 sm:px-6" v-if="store.wifiDesynced()">
          <div class="rounded-md bg-amber-50 p-4 border border-amber-300">
            <div class="flex">
              <div class="flex-shrink-0">
                <ExclamationTriangleIcon class="h-5 w-5 text-amber-600" aria-hidden="true" />
              </div>
              <div class="ml-3">
                <h3 class="text-sm font-bold text-amber-900">{{ $t('wifi.desynced_title') }}</h3>
                <p class="mt-1 text-sm text-amber-800">{{ $t('wifi.desynced_detail') }}</p>
                <p class="mt-1 text-sm text-amber-800" v-if="desyncFor">
                  {{ $t('wifi.desynced_for', { age: desyncFor, episodes: store.desyncEpisodes }) }}
                </p>
              </div>
            </div>
          </div>
        </div>

        <div class="flex flex-col">
          <div class="-my-2 overflow-x-auto sm:-mx-6 lg:-mx-8">
            <div class="py-2 align-middle inline-block min-w-full sm:px-6 lg:px-8">
              <div class="shadow overflow-hidden border-b border-gray-200 sm:rounded-lg">
                <table class="min-w-full divide-y divide-gray-200">
                  <tbody class="bg-white divide-y divide-gray-200">

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.signal') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium">
                      <span v-if="store.associated" :class="qualityClass">
                        {{ $t('wifi.signal_with_quality', { dbm: store.rssiLatest, quality: store.rssiQuality }) }}
                      </span>
                      <span v-else class="text-red-600 font-bold">{{ $t('wifi.offline') }}</span>
                    </td>
                  </tr>

                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.signal_range') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="store.rssiSamples > 0">
                        {{ $t('wifi.signal_range_value', {
                             average: store.rssiAverage,
                             minimum: store.rssiMinimum,
                             maximum: store.rssiMaximum,
                             samples: store.rssiSamples,
                             window: windowAge }) }}
                      </span>
                      <span v-else class="text-gray-500">{{ $t('wifi.no_samples') }}</span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.network') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="store.ssid">{{ $t('wifi.network_value', { ssid: store.ssid, channel: store.channel }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.none') }}</span>
                    </td>
                  </tr>

                  <!-- The row that matters on a repeated network: same SSID, different radio. -->
                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider"
                        :title="$t('wifi.access_point_hint')">
                      {{ $t('wifi.access_point') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-mono text-gray-700">
                      <span v-if="store.bssid">{{ store.bssid }}</span>
                      <span v-else class="text-gray-500 font-sans">{{ $t('sitewide.none') }}</span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.ip_address') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-mono text-gray-700">
                      <span v-if="store.ip">{{ store.ip }}</span>
                      <span v-else class="text-gray-500 font-sans">{{ $t('sitewide.none') }}</span>
                    </td>
                  </tr>

                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ store.associated ? $t('wifi.connected_for') : $t('wifi.offline_since') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium"
                        :class="store.associated ? 'text-gray-900' : 'text-red-600'">
                      <span v-if="uptimeAge">{{ uptimeAge }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.unknown') }}</span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.dropouts') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium"
                        :class="store.outages > 0 ? 'text-orange-600' : 'text-gray-900'">
                      {{ store.outages }}
                    </td>
                  </tr>

                  <!-- Only once it has happened; a permanent zero is noise on a network
                       with a single access point. -->
                  <tr class="bg-gray-50" v-if="store.roams > 0">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider"
                        :title="$t('wifi.roams_hint')">
                      {{ $t('wifi.roams') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-bold text-orange-600"
                        :title="$t('wifi.roams_hint')">
                      {{ store.roams }}
                    </td>
                  </tr>

                  <!-- Shown once anything has been ATTEMPTED, so a failing recovery is
                       visible rather than looking like one that never ran. -->
                  <tr v-if="store.roamAttempts > 0">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider"
                        :title="$t('wifi.recoveries_hint')">
                      {{ $t('wifi.recoveries') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium"
                        :class="roamHealthy ? 'text-gray-900' : 'text-orange-600'"
                        :title="$t('wifi.recoveries_hint')">
                      {{ $t('wifi.recoveries_value', {
                           landed: store.roamRecoveries,
                           attempts: store.roamAttempts,
                           result: store.roamLastResult }) }}
                      <span v-if="lastAttemptAge" class="text-gray-500">
                        {{ $t('sitewide.age_ago', { age: lastAttemptAge }) }}
                      </span>
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('wifi.last_dropout') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="lastOutageAge">
                        {{ $t('wifi.last_dropout_value', { age: lastOutageAge, duration: lastOutageDuration }) }}
                      </span>
                      <span v-else class="text-gray-500">{{ $t('wifi.no_dropouts') }}</span>
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
import { computed, onMounted, onBeforeUnmount, ref } from "vue";
import { ExclamationTriangleIcon } from "@heroicons/vue/24/outline";
import { useWifiLinkStore } from "@/stores/WifiLinkStore";
import { rssiQualityClass } from "@/mixins/TiltDevice";
import { formatAge } from "@/mixins/FormatAge";

const store = useWifiLinkStore();
const intervalObject = ref(null);

const qualityClass = computed(() => rssiQualityClass(store.rssiQuality));
const windowAge = computed(() => formatAge(store.windowSec) ?? '--');
const lastOutageAge = computed(() => formatAge(store.lastOutageAgoSec));
const lastAttemptAge = computed(() => formatAge(store.roamLastAttemptAgoSec));
const desyncFor = computed(() => formatAge(store.desyncCurrentSec));

/*
 * An attempt that ended in anything but LANDED or NO_CANDIDATE is a failure worth colouring.
 * NO_CANDIDATE is a correct, healthy outcome: the link is weak everywhere and there is
 * genuinely nothing better to move to.
 */
const roamHealthy = computed(() =>
    store.roamLastResult === 'LANDED' || store.roamLastResult === 'NO_CANDIDATE');
const lastOutageDuration = computed(() => formatAge(store.lastOutageDurationSec) ?? '--');

// One row for both states: how long the link has held, or how long it has been down.
const uptimeAge = computed(() =>
    store.associated ? formatAge(store.connectedForSec) : formatAge(store.currentOutageSec));

onMounted(() => {
  store.getWifiLink();

  // 10 s, matching the firmware's RSSI sampling interval. Polling faster would only
  // re-fetch the same sample.
  intervalObject.value = window.setInterval(() => {
    store.getWifiLink();
  }, 10000);
});

onBeforeUnmount(() => {
  clearInterval(intervalObject.value);
});
</script>

<style scoped>
</style>
