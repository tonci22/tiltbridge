<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.google_sheets.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="google_sheets" />
        </div>

        <form @submit.prevent="submitForm">

          <div class="px-4 py-5">
            <!-- Script URL Field -->
            <TextField v-model="gs_url" placeholder="https://script.google.com/.../">
              <template #FieldName>{{ $t('cloud_config.google_sheets.url') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.google_sheets.url_desc') }}</template>
            </TextField>

            <!-- Script Email Field -->
            <TextField v-model="gs_email" placeholder="you@gmail.com">
              <template #FieldName>{{ $t('cloud_config.google_sheets.email') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.google_sheets.email_desc') }}</template>
            </TextField>

            <!-- Sheet names are per Tilt now, set from the Tilts page. -->

            <!-- Upload interval. Not the queue snapshot interval, which lives on the Queue page. -->
            <PushIntervalField v-model="gsPushEvery">
              <template #FieldName>{{ $t('cloud_config.google_sheets.push_frequency') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.google_sheets.push_frequency_desc') }}</template>
            </PushIntervalField>









            <!-- v2 protocol opt-in (CapturedAtUtc / TimestampValid payload) -->
            <fieldset class="space-y-5 mt-2">
              <CheckboxField v-model="v2Enabled">
                <template #FieldName>{{ $t('cloud_config.google_sheets.v2_enabled') }}</template>
                <template #FieldDescription>{{ $t('cloud_config.google_sheets.v2_enabled_desc') }}</template>
              </CheckboxField>
            </fieldset>

            <FormErrorMsg :form-error-message="form_error_message" v-if="form_error_message.length > 0" />

          </div>

          <!-- Card Footer -->
          <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
            <button type="submit" class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:ml-3 sm:w-auto sm:text-sm" >
              {{ $t('sitewide.update') }}
            </button>
          </div>

        </form>

      </div>
    </div>
  </div>

  <UpdateSuccessfulModal update-successful="update-successful" v-model="alertOpen" />

</template>

<script setup>
import FormErrorMsg from "@/components/generic/FormErrorMsg.vue";
import SendTargetErrorMsg from "@/components/generic/SendTargetErrorMsg.vue";
import { useConfigStore } from "@/stores/ConfigStore";
import { useLoading } from 'vue-loading-overlay'
import TextField from "@/components/config/form_elements/TextField.vue";
import CheckboxField from "@/components/config/form_elements/CheckboxField.vue";
import PushIntervalField from "@/components/config/form_elements/PushIntervalField.vue";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";
import { ref } from "vue";
import { i18n } from "@/main.js";

const $loading = useLoading({
  // options
});

const updateSuccessful = ref(false);
const alertOpen = ref(false);
const configStore = useConfigStore();

let form_error_message = ref("");


const gs_url = ref(configStore.scriptsURL);
const gs_email = ref(configStore.scriptsEmail);
const v2Enabled = ref(configStore.gsheetsV2Enabled);
const gsPushEvery = ref(configStore.gsheetsPushEvery);

function updateCachedSettings() {
  gs_url.value = configStore.scriptsURL;
  gs_email.value = configStore.scriptsEmail;
  v2Enabled.value = configStore.gsheetsV2Enabled;
  gsPushEvery.value = configStore.gsheetsPushEvery;
}


async function submitForm() {
  form_error_message.value = "";

  // TODO - Zero out sheet values if url/email are unset

  // If there isn't a validation error, submit the form
  let loader = $loading.show({});
  configStore.updateGoogleSheetsConfig(
      gs_url.value,
      gs_email.value,
      v2Enabled.value,
      gsPushEvery.value
  ).then(() => {
    updateCachedSettings();
    loader.hide();
    updateSuccessful.value = !configStore.configUpdateError;  // configUpdateError is inverted from what we want here
    alertOpen.value = true;
  });
}

</script>


<style scoped>

</style>