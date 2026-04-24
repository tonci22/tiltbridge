<template>
  <div v-if="failingTargets.length > 0" class="rounded-md bg-red-50 p-4">
    <div class="flex">
      <div class="flex-shrink-0">
        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
      </div>
      <div class="ml-3">
        <h3 class="text-sm font-medium text-red-800">
          {{ $t('send_errors.banner_title') }}
        </h3>
        <div class="mt-2 text-sm text-red-700">
          <ul class="list-disc pl-5 space-y-1">
            <li v-for="item in failingTargets" :key="item.key">
              <router-link
                :to="{ name: item.routeName }"
                class="font-medium underline hover:text-red-900"
              >
                {{ $t(`send_errors.targets.${item.key}`) }}
              </router-link>
              &mdash; {{ shortError(item.errorCode) }}
            </li>
          </ul>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { ExclamationTriangleIcon } from '@heroicons/vue/20/solid';
import { useSendTargetErrorStore, TARGET_META } from '@/stores/SendTargetErrorStore';

const { t } = useI18n();
const errorStore = useSendTargetErrorStore();

const failingTargets = computed(() => {
  return Object.entries(errorStore.targets)
    .filter(([, target]) => target.error_code !== 0)
    .filter(([key]) => key in TARGET_META)
    .map(([key, target]) => ({
      key,
      errorCode: target.error_code,
      routeName: TARGET_META[key].routeName,
    }));
});

function shortError(code) {
  const key = `send_errors.error_short.code_${code}`;
  if (t(key) !== key) return t(key);
  return t('send_errors.error_short.unknown', { code });
}
</script>
