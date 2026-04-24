<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.brewers_friend.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="brewers_friend" />
        </div>

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h4 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.brewers_friend.about_header') }}
          </h4>

          <p>
            {{ $t('cloud_config.brewers_friend.about_text') }}
          </p>
        </div>

        <form @submit.prevent="submitForm">
          <div class="px-4 py-5">

            <!-- API Key Field -->
            <TextField v-model="brewersFriendKey" placeholder="Your Brewers Friend API Key">
              <template #FieldName>{{ $t('cloud_config.brewers_friend.api_key') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.brewers_friend.api_key_desc') }}</template>
            </TextField>

            <FormErrorMsg :form-error-message="form_error_message" v-if="form_error_message.length > 0" />
          </div>

          <!-- Card Footer -->
          <div class="bg-white px-4 py-5 border-t border-gray-200 sm:px-6 sm:flex sm:flex-row-reverse">
            <button type="submit" class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:ml-3 sm:w-auto sm:text-sm">
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
import { ref } from "vue";
import { useConfigStore } from "@/stores/ConfigStore";
import TextField from "@/components/config/form_elements/TextField.vue";
import FormErrorMsg from "@/components/generic/FormErrorMsg.vue";
import SendTargetErrorMsg from "@/components/generic/SendTargetErrorMsg.vue";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";
import {useLoading} from "vue-loading-overlay";
import {i18n} from "@/main";

const configStore = useConfigStore();
const brewersFriendKey = ref(configStore.brewersFriendKey);
let form_error_message = ref("");

const updateSuccessful = ref(false);
const alertOpen = ref(false);

const $loading = useLoading({
  // options
});

function updateCachedSettings() {
  brewersFriendKey.value = configStore.brewersFriendKey;
}

async function submitForm() {
  form_error_message.value = "";

  if (brewersFriendKey.value && brewersFriendKey.value.length <= 12) {
    form_error_message.value = i18n.global.t('cloud_config.brewers_friend.error_api_key_too_short');
    return;
  }

  let loader = $loading.show({});

  configStore.updateBrewersFriendConfig(brewersFriendKey.value).then(() => {
    // updateCachedSettings();
    loader.hide();
    updateSuccessful.value = !configStore.configUpdateError;  // configUpdateError is inverted from what we want here
    alertOpen.value = true;
  });

}
</script>
