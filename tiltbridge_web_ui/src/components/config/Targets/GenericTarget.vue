<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.generic_target.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="user_target" />
        </div>

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h4 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.generic_target.about_header') }}
          </h4>
          <p>
            {{ $t('cloud_config.generic_target.about_text') }}
          </p>
        </div>

        <form @submit.prevent="submitForm">
          <div class="px-4 py-5">
            <!-- Generic Target URL Field -->
            <TextField v-model="genericTargetURL" placeholder="http://192.168.1.2/api/data">
              <template #FieldName>{{ $t('cloud_config.generic_target.generic_target_url') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.generic_target.generic_target_url_desc') }}</template>
            </TextField>

            <!-- Generic Target Push Frequency Field -->
            <PushIntervalField v-model="genericTargetPushEvery">
              <template #FieldName>{{ $t('cloud_config.generic_target.push_frequency') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.generic_target.push_frequency_desc') }}</template>
            </PushIntervalField>

            <FormErrorMsg :form-error-message="formErrorMessage" v-if="formErrorMessage.length > 0" />
          </div>

          <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
            <button type="submit" class="inline-flex justify-center py-2 px-4 border border-transparent shadow-sm text-sm font-medium rounded-md text-white bg-blue-600 hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500">
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
import { ref } from 'vue';
import TextField from '@/components/config/form_elements/TextField.vue';
import PushIntervalField from '@/components/config/form_elements/PushIntervalField.vue';
import FormErrorMsg from '@/components/generic/FormErrorMsg.vue';
import SendTargetErrorMsg from "@/components/generic/SendTargetErrorMsg.vue";
import { useConfigStore } from '@/stores/ConfigStore';
import {useLoading} from "vue-loading-overlay";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";

const configStore = useConfigStore();
const genericTargetURL = ref(configStore.genericTargetURL);
const genericTargetPushEvery = ref(configStore.userTargetPushEvery);
const formErrorMessage = ref('');

const updateSuccessful = ref(false);
const alertOpen = ref(false);

const $loading = useLoading({
  // options
});

function updateCachedSettings() {
  genericTargetURL.value = configStore.genericTargetURL;
  genericTargetPushEvery.value = configStore.userTargetPushEvery;
}

async function submitForm() {
  formErrorMessage.value = '';

  let loader = $loading.show({});

  configStore.updateGenericTargetConfig(genericTargetURL.value, genericTargetPushEvery.value).then(() => {
    updateCachedSettings();
    loader.hide();
    updateSuccessful.value = !configStore.configUpdateError;  // configUpdateError is inverted from what we want here
    alertOpen.value = true;
  });
}
</script>
