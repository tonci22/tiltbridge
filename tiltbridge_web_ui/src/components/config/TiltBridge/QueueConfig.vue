<template>
  <div>
    <!-- Status first, so state and knobs are visible together (docs/phase1/09-web-ui.md §23). -->
    <QueueStatusPanel />

    <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
      <div class="flex-initial md:container">
        <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

          <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
            <h3 class="text-lg leading-6 font-medium text-gray-900">
              {{ $t('queue.settings.header') }}
            </h3>
            <p class="mt-1 max-w-2xl text-sm text-gray-500">
              {{ $t('queue.settings.description') }}
            </p>
          </div>

          <form @submit.prevent="submitForm">
            <div class="px-4 py-5">

              <!-- Snapshot interval: how often the queue is written to flash -->
              <div class="rounded-md bg-blue-50 px-4 py-3 mb-4">
                <h4 class="text-sm font-medium text-blue-900">{{ $t('queue.settings.interval_group') }}</h4>
                <p class="mt-1 text-sm text-blue-800">{{ $t('queue.settings.interval_group_desc') }}</p>
              </div>

              <SelectField v-model="intervalSelection">
                <template #FieldName>{{ $t('queue.settings.snapshot_interval') }}</template>
                <template #FieldDescription>{{ $t('queue.settings.snapshot_interval_desc') }}</template>
                <template #FieldOptions>
                  <option value="10">{{ $t('queue.settings.minutes_option', { minutes: 10 }) }}</option>
                  <option value="15">{{ $t('queue.settings.minutes_option', { minutes: 15 }) }}</option>
                  <option value="30">{{ $t('queue.settings.minutes_option', { minutes: 30 }) }}</option>
                  <option value="60">{{ $t('queue.settings.minutes_option', { minutes: 60 }) }}</option>
                  <option value="custom">{{ $t('queue.settings.custom_option') }}</option>
                </template>
              </SelectField>

              <TextField v-model="customIntervalMinutes" v-if="intervalSelection === 'custom'" placeholder="30">
                <template #FieldName>{{ $t('queue.settings.custom_interval') }}</template>
                <template #FieldDescription>{{ $t('queue.settings.custom_interval_desc') }}</template>
              </TextField>

              <!-- Maximum records: how much history survives an outage -->
              <div class="rounded-md bg-blue-50 px-4 py-3 mb-4 mt-6">
                <h4 class="text-sm font-medium text-blue-900">{{ $t('queue.settings.capacity_group') }}</h4>
                <p class="mt-1 text-sm text-blue-800">{{ $t('queue.settings.capacity_group_desc') }}</p>
              </div>

              <TextField v-model="maxRecords" placeholder="1500">
                <template #FieldName>{{ $t('queue.settings.max_records') }}</template>
                <template #FieldDescription>{{ $t('queue.settings.max_records_desc', { bytes: estimatedBytes }) }}</template>
              </TextField>

              <TextField v-model="batchSize" placeholder="20">
                <template #FieldName>{{ $t('queue.settings.batch_size') }}</template>
                <template #FieldDescription>{{ $t('queue.settings.batch_size_desc') }}</template>
              </TextField>

              <fieldset class="space-y-5 mt-6">
                <CheckboxField v-model="queueEnabled">
                  <template #FieldName>{{ $t('queue.settings.enable_queue') }}</template>
                  <template #FieldDescription>{{ $t('queue.settings.enable_queue_desc') }}</template>
                </CheckboxField>
              </fieldset>

              <FormErrorMsg :form-error-message="form_error_message" v-if="form_error_message.length > 0" />
            </div>

            <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
              <button type="submit" class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:ml-3 sm:w-auto sm:text-sm">
                {{ $t('sitewide.update') }}
              </button>
            </div>
          </form>

        </div>
      </div>
    </div>

    <!-- Sender watchdog. Lives here rather than on the general settings page because it is the
         other half of "do not lose readings when the sender wedges". -->
    <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
      <div class="flex-initial md:container">
        <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

          <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
            <h3 class="text-lg leading-6 font-medium text-gray-900">
              {{ $t('queue.recovery.header') }}
            </h3>
            <p class="mt-1 max-w-2xl text-sm text-gray-500">
              {{ $t('queue.recovery.description') }}
            </p>
          </div>

          <form @submit.prevent="submitRecoveryForm">
            <div class="px-4 py-5">
              <TextField v-model="staleRebootSec" placeholder="75">
                <template #FieldName>{{ $t('queue.recovery.stale_reboot_sec') }}</template>
                <template #FieldDescription>{{ $t('queue.recovery.stale_reboot_sec_desc') }}</template>
              </TextField>

              <fieldset class="space-y-5 mt-2">
                <CheckboxField v-model="recoveryEnabled">
                  <template #FieldName>{{ $t('queue.recovery.enable_recovery') }}</template>
                  <template #FieldDescription>{{ $t('queue.recovery.enable_recovery_desc') }}</template>
                </CheckboxField>
              </fieldset>

              <FormErrorMsg :form-error-message="recovery_error_message" v-if="recovery_error_message.length > 0" />
            </div>

            <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
              <button type="submit" class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:ml-3 sm:w-auto sm:text-sm">
                {{ $t('sitewide.update') }}
              </button>
            </div>
          </form>

        </div>
      </div>
    </div>

    <UpdateSuccessfulModal v-model="alertOpen" :update-successful="updateSuccessful" />
  </div>
</template>

<script setup>
import { computed, onMounted, onBeforeUnmount, ref, watch } from "vue";
import { useLoading } from 'vue-loading-overlay';
import QueueStatusPanel from "@/components/config/TiltBridge/QueueStatusPanel.vue";
import TextField from "@/components/config/form_elements/TextField.vue";
import SelectField from "@/components/config/form_elements/SelectField.vue";
import CheckboxField from "@/components/config/form_elements/CheckboxField.vue";
import FormErrorMsg from "@/components/generic/FormErrorMsg.vue";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";
import { useQueueStore } from "@/stores/QueueStore";
import { useConfigStore } from "@/stores/ConfigStore";
import { i18n } from "@/main.js";

const $loading = useLoading({});

const queueStore = useQueueStore();
const configStore = useConfigStore();

const PRESET_MINUTES = ["10", "15", "30", "60"];

let intervalObject = null;

const intervalSelection = ref("30");
const customIntervalMinutes = ref("30");
const maxRecords = ref("1500");
const batchSize = ref("20");
const queueEnabled = ref(false);

const recoveryEnabled = ref(true);
const staleRebootSec = ref("75");

const form_error_message = ref("");
const recovery_error_message = ref("");
const alertOpen = ref(false);
const updateSuccessful = ref(false);

const estimatedBytes = computed(() => {
  const records = parseInt(maxRecords.value, 10);
  const size = queueStore.recordSize || 0;
  if (!Number.isFinite(records) || size === 0) return "--";
  return (records * size).toLocaleString();
});

/**
 * The API is in seconds; the form is in minutes. Convert only here and in submitForm.
 */
function seedQueueForm() {
  const minutes = Math.round((queueStore.snapshotIntervalSec || 1800) / 60);
  const minutesStr = String(minutes);
  if (PRESET_MINUTES.includes(minutesStr)) {
    intervalSelection.value = minutesStr;
  } else {
    intervalSelection.value = "custom";
  }
  customIntervalMinutes.value = minutesStr;
  maxRecords.value = String(queueStore.maxRecords);
  batchSize.value = String(queueStore.batchSize);
  queueEnabled.value = queueStore.enabled;
}

function seedRecoveryForm() {
  recoveryEnabled.value = configStore.senderRecoveryEnabled;
  staleRebootSec.value = String(configStore.senderStaleRebootSec);
}

function selectedIntervalMinutes() {
  if (intervalSelection.value === "custom") {
    return parseInt(customIntervalMinutes.value, 10);
  }
  return parseInt(intervalSelection.value, 10);
}

async function submitForm() {
  form_error_message.value = "";

  const minutes = selectedIntervalMinutes();
  if (!Number.isFinite(minutes) || minutes < 1 || minutes > 360) {
    form_error_message.value = i18n.global.t('queue.errors.invalid_interval');
    return;
  }

  const records = parseInt(maxRecords.value, 10);
  if (!Number.isFinite(records) || records < 100 || records > 3000) {
    form_error_message.value = i18n.global.t('queue.errors.invalid_max_records');
    return;
  }

  const batch = parseInt(batchSize.value, 10);
  if (!Number.isFinite(batch) || batch < 1 || batch > 50) {
    form_error_message.value = i18n.global.t('queue.errors.invalid_batch_size');
    return;
  }

  const loader = $loading.show({});
  const success = await queueStore.updateQueueSettings({
    snapshotIntervalSeconds: minutes * 60,
    maximumRecords: records,
    batch: batch,
    queueEnabled: queueEnabled.value,
  });
  loader.hide();

  seedQueueForm();
  updateSuccessful.value = success;
  alertOpen.value = true;
}

async function submitRecoveryForm() {
  recovery_error_message.value = "";

  const staleSec = parseInt(staleRebootSec.value, 10);
  if (!Number.isFinite(staleSec) || staleSec < 60 || staleSec > 600) {
    recovery_error_message.value = i18n.global.t('queue.errors.invalid_stale_reboot');
    return;
  }

  const loader = $loading.show({});
  const success = await configStore.updateSenderRecoveryConfig(recoveryEnabled.value, staleSec);
  loader.hide();

  updateSuccessful.value = success;
  alertOpen.value = true;
}

// Seed the form the first time real values arrive, then leave it alone so a poll cannot
// overwrite what the user is typing.
const queueSeeded = ref(false);
watch(() => queueStore.loaded, (isLoaded) => {
  if (isLoaded && !queueSeeded.value) {
    queueSeeded.value = true;
    seedQueueForm();
  }
}, { immediate: true });

const recoverySeeded = ref(false);
watch(() => configStore.loaded, (isLoaded) => {
  if (isLoaded && !recoverySeeded.value) {
    recoverySeeded.value = true;
    seedRecoveryForm();
  }
}, { immediate: true });

onMounted(() => {
  queueStore.getQueueStatus();
  if (!configStore.loaded) {
    configStore.getConfig();
  }

  // Same cadence as the Tilt list.
  intervalObject = window.setInterval(() => {
    queueStore.getQueueStatus();
  }, 15000);
});

onBeforeUnmount(() => {
  clearInterval(intervalObject);
});
</script>

<style scoped>

</style>
