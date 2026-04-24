<template>
  <div v-if="hasError" class="rounded-md bg-yellow-50 p-4">
    <div class="flex">
      <div class="flex-shrink-0">
        <ExclamationTriangleIcon class="h-5 w-5 text-yellow-400" aria-hidden="true" />
      </div>
      <div class="ml-3">
        <h3 class="text-sm font-medium text-yellow-800">
          {{ $t('send_errors.config_error_title') }}: {{ errorSummary }}
        </h3>
        <div v-if="errorExplanation" class="mt-2 text-sm text-yellow-700">
          <p>{{ errorExplanation }}</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { ExclamationTriangleIcon } from '@heroicons/vue/20/solid';
import { useSendTargetErrorStore } from '@/stores/SendTargetErrorStore';

const props = defineProps({
  targetKey: {
    type: String,
    required: true,
  },
});

const { t } = useI18n();
const errorStore = useSendTargetErrorStore();

onMounted(() => {
  if (!errorStore.loaded) {
    errorStore.getErrors();
  }
});

const hasError = computed(() => errorStore.isTargetError(props.targetKey));

const errorCode = computed(() => {
  const target = errorStore.getTargetError(props.targetKey);
  return target ? target.error_code : 0;
});

const errorSummary = computed(() => {
  const code = errorCode.value;
  const key = `send_errors.error_short.code_${code}`;
  if (t(key) !== key) return t(key);
  return t('send_errors.error_short.unknown', { code });
});

const errorExplanation = computed(() => {
  const code = errorCode.value;
  const key = `send_errors.error_detail.code_${code}`;
  if (t(key) !== key) return t(key);
  return t('send_errors.error_detail.unknown', { code });
});
</script>
