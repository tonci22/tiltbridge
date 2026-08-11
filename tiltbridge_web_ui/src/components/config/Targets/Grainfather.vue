<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.grainfather.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="grainfather" />
        </div>

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h4 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.grainfather.about_header') }}
          </h4>
          <p>
            {{ $t('cloud_config.grainfather.about_text') }}
          </p>
        </div>

        <form @submit.prevent="submitForm">
          <div class="px-4 py-5">
            <!-- Dynamic fields for each Tilt color -->
            <div v-for="color in tiltColors" :key="color.name" class="mb-4">
              <TextField v-model="grainfatherUrls[color.name]" placeholder="https://community.grainfather.com/iot/xxxx-xxxx/tilt">
                <template #FieldName>{{ color.displayName }}</template>
                <template #FieldDescription>
                  <i18n-t keypath="cloud_config.grainfather.grainfather_url_desc" tag="span">
                    <template v-slot:color>{{ color.displayName }}</template>
                  </i18n-t>
                </template>
              </TextField>
            </div>

            <PushIntervalField v-model="pushEvery">
              <template #FieldName>{{ $t('cloud_config.grainfather.push_frequency') }}</template>
              <template #FieldDescription>{{ $t('cloud_config.grainfather.push_frequency_desc') }}</template>
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
import {i18n} from "@/main";
import {useLoading} from "vue-loading-overlay";
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";

const configStore = useConfigStore();
const formErrorMessage = ref('');

const updateSuccessful = ref(false);
const alertOpen = ref(false);

const $loading = useLoading({
  // options
});

const tiltColors = [
  { name: 'Red', displayName: i18n.global.t('sitewide.tilt_colors.red') },
  { name: 'Green', displayName: i18n.global.t('sitewide.tilt_colors.green') },
  { name: 'Black', displayName: i18n.global.t('sitewide.tilt_colors.black') },
  { name: 'Purple', displayName: i18n.global.t('sitewide.tilt_colors.purple') },
  { name: 'Orange', displayName: i18n.global.t('sitewide.tilt_colors.orange') },
  { name: 'Blue', displayName: i18n.global.t('sitewide.tilt_colors.blue') },
  { name: 'Yellow', displayName: i18n.global.t('sitewide.tilt_colors.yellow') },
  { name: 'Pink', displayName: i18n.global.t('sitewide.tilt_colors.pink') }
];

const grainfatherUrls = ref({
  Red: configStore.grainfatherRedURL,
  Green: configStore.grainfatherGreenURL,
  Black: configStore.grainfatherBlackURL,
  Purple: configStore.grainfatherPurpleURL,
  Orange: configStore.grainfatherOrangeURL,
  Blue: configStore.grainfatherBlueURL,
  Yellow: configStore.grainfatherYellowURL,
  Pink: configStore.grainfatherPinkURL,
});

const pushEvery = ref(configStore.grainfatherPushEvery);


async function submitForm() {
  formErrorMessage.value = '';

  let loader = $loading.show({});

  // Trim grainfather URLs, as their UI appends a space to the end for some reason
  Object.keys(grainfatherUrls.value).forEach(key => {
    grainfatherUrls.value[key] = grainfatherUrls.value[key].trim();
  });


  configStore.updateGrainfatherUrls(grainfatherUrls.value, pushEvery.value).then(() => {
    // updateCachedSettings();
    loader.hide();
    updateSuccessful.value = !configStore.configUpdateError;  // configUpdateError is inverted from what we want here
    alertOpen.value = true;
  });



}
</script>
