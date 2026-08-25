<template>
  <!-- Nothing at all until the first payload lands, rather than a moment of "offline" that
       is only ever an artefact of the page having just loaded. -->
  <div v-if="store.loaded" class="flex items-center gap-2" :title="tooltip">

    <template v-if="store.associated">
      <span :class="qualityClass" class="flex items-center gap-2">
        <svg viewBox="0 0 20 16" class="h-4 w-5 flex-none" aria-hidden="true">
          <rect x="0"    y="11"  width="3.5" height="5"    rx="0.7" :class="barClass(1)" />
          <rect x="5"    y="8"   width="3.5" height="8"    rx="0.7" :class="barClass(2)" />
          <rect x="10"   y="4.5" width="3.5" height="11.5" rx="0.7" :class="barClass(3)" />
          <rect x="15"   y="1"   width="3.5" height="15"   rx="0.7" :class="barClass(4)" />
        </svg>
        <span class="text-sm font-semibold whitespace-nowrap">
          {{ $t('wifi.signal_value', { dbm: store.rssiLatest }) }}
        </span>
        <span class="text-xs font-medium uppercase tracking-wide whitespace-nowrap">
          {{ store.rssiQuality }}
        </span>
      </span>

      <span class="hidden sm:inline text-xs text-gray-500 whitespace-nowrap">
        {{ networkLine }}
      </span>

      <!-- The manager reporting down while the interface is associated is the exact
           condition that silently disables every upload target. Worth a badge. -->
      <span v-if="store.wifiDesynced()"
            class="inline-flex items-center gap-1 rounded bg-amber-100 px-1.5 py-0.5 text-xs font-medium text-amber-800">
        <ExclamationTriangleIcon class="h-3.5 w-3.5" aria-hidden="true" />
        <span class="hidden sm:inline">{{ $t('wifi.desynced_short') }}</span>
      </span>
    </template>

    <template v-else>
      <span class="flex items-center gap-2 text-red-600">
        <SignalSlashIcon class="h-5 w-5 flex-none" aria-hidden="true" />
        <span class="text-sm font-semibold whitespace-nowrap">{{ $t('wifi.offline') }}</span>
      </span>
      <span v-if="offlineFor" class="hidden sm:inline text-xs text-gray-500 whitespace-nowrap">
        {{ offlineFor }}
      </span>
    </template>

  </div>
</template>

<script setup>
import { computed } from "vue";
import { ExclamationTriangleIcon, SignalSlashIcon } from "@heroicons/vue/24/outline";
import { useWifiLinkStore } from "@/stores/WifiLinkStore";
import { rssiQualityClass } from "@/mixins/TiltDevice";
import { formatAge } from "@/mixins/FormatAge";
import { i18n } from "@/main.js";

const store = useWifiLinkStore();

/*
 * How many of the four bars are lit. The dBm thresholds behind the quality name live in
 * firmware (src/rssi_stats.h) so they exist in one place only; this maps the name it
 * reports onto a bar count. CRITICAL and WEAK both light one bar - there is no useful
 * distinction to draw at that width, and the colour already carries the severity.
 */
const QUALITY_BARS = {
  EXCELLENT: 4,
  GOOD: 3,
  FAIR: 2,
  WEAK: 1,
  CRITICAL: 1,
};

const bars = computed(() => QUALITY_BARS[store.rssiQuality] ?? 0);
const qualityClass = computed(() => rssiQualityClass(store.rssiQuality));

// Lit bars inherit the quality colour through fill-current; the rest stay grey.
function barClass(index) {
  return index <= bars.value ? 'fill-current' : 'fill-gray-200';
}

const networkLine = computed(() => {
  if (!store.ssid) return '';
  if (store.channel === null || store.channel === undefined) return store.ssid;
  return i18n.global.t('wifi.network_line', { ssid: store.ssid, channel: store.channel });
});

const offlineFor = computed(() => {
  const age = formatAge(store.currentOutageSec);
  return age ? i18n.global.t('wifi.offline_for', { age }) : null;
});

/*
 * The aggregates go in the tooltip rather than the chip: a single instantaneous RSSI bounces
 * by ten dBm or more between samples, so the window's average and spread are what actually
 * say whether the link is steady.
 */
const tooltip = computed(() => {
  if (!store.associated) {
    return i18n.global.t('wifi.tooltip_offline', { outages: store.outages });
  }
  if (!store.rssiSamples) {
    return i18n.global.t('wifi.tooltip_no_samples');
  }
  return i18n.global.t('wifi.tooltip', {
    average: store.rssiAverage ?? '--',
    minimum: store.rssiMinimum ?? '--',
    maximum: store.rssiMaximum ?? '--',
    samples: store.rssiSamples,
    window: formatAge(store.windowSec) ?? '--',
    outages: store.outages,
  });
});
</script>

<style scoped>
</style>
