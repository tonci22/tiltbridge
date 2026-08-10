<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('about.sender.header') }}
          </h3>
        </div>

        <!-- The whole reason this fork exists: proof that the watchdog fired. Kept visible for
             the entire boot session, not just for a moment after the event. -->
        <div class="px-4 pt-4 sm:px-6" v-if="SenderHealthStore.lastRecovery">
          <div class="rounded-md bg-amber-50 p-4 border border-amber-300">
            <div class="flex">
              <div class="flex-shrink-0">
                <ArrowPathIcon class="h-5 w-5 text-amber-600" aria-hidden="true" />
              </div>
              <div class="ml-3">
                <h3 class="text-sm font-bold text-amber-900">
                  {{ $t('about.sender.last_recovery_title') }}
                </h3>
                <p class="mt-1 text-sm text-amber-800">
                  {{ recoveryReasonText }}
                </p>
                <p class="mt-1 text-sm text-amber-800">
                  {{ recoveryDetailText }}
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
                      {{ $t('about.sender.state') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm" :class="stateClass">
                      {{ stateLabel }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.current_target') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ targetLabel(SenderHealthStore.currentTarget) }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.heartbeat') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ $t('sitewide.age_ago', { age: formatAge(SenderHealthStore.heartbeatAgeSec) }) }}
                    </td>
                  </tr>

                  <tr>
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.send_lock') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ SenderHealthStore.lockHeld ? $t('about.sender.lock_held') : $t('about.sender.lock_free') }}
                    </td>
                  </tr>

                  <!-- Only shown while a lock is actually held, so the idle panel stays short. -->
                  <tr v-if="SenderHealthStore.lockHeld">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.request_age') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ formatAge(SenderHealthStore.requestAgeSec) }}
                    </td>
                  </tr>

                  <tr v-if="SenderHealthStore.lockHeld">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.lock_age') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      {{ formatAge(SenderHealthStore.lockAgeSec) }}
                    </td>
                  </tr>

                  <tr v-if="SenderHealthStore.consecutiveSendFailures > 0">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.consecutive_failures') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-bold text-orange-600">
                      {{ SenderHealthStore.consecutiveSendFailures }}
                    </td>
                  </tr>

                  <!-- A climbing counter here confirms the stale wifi_cfg_is_connected()
                       diagnosis in docs/phase1/00-OVERVIEW.md. -->
                  <tr v-if="SenderHealthStore.wifiFlagDisagreements > 0">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider" :title="$t('about.sender.wifi_disagreements_tooltip')">
                      {{ $t('about.sender.wifi_disagreements') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-bold text-amber-600" :title="$t('about.sender.wifi_disagreements_tooltip')">
                      {{ SenderHealthStore.wifiFlagDisagreements }}
                    </td>
                  </tr>

                  <tr v-if="SenderHealthStore.staleEvents > 0">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.stale_events') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-bold text-amber-600">
                      {{ SenderHealthStore.staleEvents }}
                    </td>
                  </tr>

                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.google_last_success') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="googleAge">{{ $t('sitewide.age_ago', { age: googleAge }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.never') }}</span>
                    </td>
                  </tr>

                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.fermentrack_last_success') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="fermentrackAge">{{ $t('sitewide.age_ago', { age: fermentrackAge }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.never') }}</span>
                    </td>
                  </tr>

                  <tr class="bg-gray-50">
                    <th scope="row" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      {{ $t('about.sender.any_last_success') }}
                    </th>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                      <span v-if="anyAge">{{ $t('sitewide.age_ago', { age: anyAge }) }}</span>
                      <span v-else class="text-gray-500">{{ $t('sitewide.never') }}</span>
                    </td>
                  </tr>

                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>

        <p class="px-6 py-4 text-sm text-red-700" v-if="SenderHealthStore.senderError">
          {{ $t('about.sender.load_failed') }}
        </p>

      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onBeforeUnmount, ref } from 'vue';
import { ArrowPathIcon } from "@heroicons/vue/24/outline";
import { useSenderHealthStore } from "@/stores/SenderHealthStore";
import { formatAge, formatAgeMs } from "@/mixins/FormatAge";
import { i18n } from "@/main.js";

const SenderHealthStore = useSenderHealthStore();
const senderIntervalObject = ref(null);

const stateLabel = computed(() => {
  const key = `about.sender.states.${(SenderHealthStore.state || 'IDLE').toLowerCase()}`;
  return i18n.global.te(key) ? i18n.global.t(key) : SenderHealthStore.state;
});

const stateClass = computed(() => {
  if (SenderHealthStore.state === 'STALE') return 'font-bold text-red-600';
  if (SenderHealthStore.state === 'SENDING') return 'font-medium text-green-700';
  return 'font-medium text-green-700';
});

function targetLabel(target) {
  if (!target) return i18n.global.t('sitewide.none');
  const key = `send_errors.targets.${target}`;
  return i18n.global.te(key) ? i18n.global.t(key) : target;
}

const googleAge = computed(() => formatAge(SenderHealthStore.lastGoogleSuccessAgeSec));
const fermentrackAge = computed(() => formatAge(SenderHealthStore.lastFermentrackSuccessAgeSec));
const anyAge = computed(() => formatAge(SenderHealthStore.lastAnySuccessAgeSec));

const recoveryReasonText = computed(() => {
  const rec = SenderHealthStore.lastRecovery;
  if (!rec) return "";
  const key = `about.sender.recovery_reasons.${rec.reason}`;
  const reason = i18n.global.te(key) ? i18n.global.t(key) : rec.reason;
  return i18n.global.t('about.sender.last_recovery_line', {
    reason: reason,
    target: rec.target ? targetLabel(rec.target) : i18n.global.t('sitewide.none'),
  });
});

const recoveryDetailText = computed(() => {
  const rec = SenderHealthStore.lastRecovery;
  if (!rec) return "";
  return i18n.global.t('about.sender.last_recovery_detail', {
    heartbeat: formatAgeMs(rec.heartbeatAgeMs) ?? '--',
    lock: formatAgeMs(rec.lockAgeMs) ?? '--',
    uptime: formatAge(rec.uptimeSecAtReboot) ?? '--',
    boot: rec.bootCount ?? '--',
  });
});

onMounted(() => {
  SenderHealthStore.getSenderHealth();

  // Faster than the queue - this is the panel a user stares at when the device misbehaves.
  senderIntervalObject.value = window.setInterval(() => {
    SenderHealthStore.getSenderHealth();
  }, 10000);
});

onBeforeUnmount(() => {
  clearInterval(senderIntervalObject.value);
});
</script>

<style scoped>

</style>
