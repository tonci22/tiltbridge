<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.influxdb.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="influxdb" />
        </div>

        <form @submit.prevent="submitForm">

          <div class="py-5 px-4">

            <div class="space-y-8 divide-y divide-gray-200">
              <div class="pt-8">
                <div>
                  <h3 class="text-lg leading-6 font-medium text-gray-900">{{ $t('cloud_config.influxdb.about_header') }}</h3>
                  <p class="mt-1 text-sm text-gray-600">
                    {{ $t('cloud_config.influxdb.about_text') }}
                  </p>
                </div>

                <div class="mt-6 grid grid-cols-1 gap-y-6 gap-x-4 sm:grid-cols-6">
                  
                  <div class="col-span-6 lg:col-span-4">
                    <label for="influxdb-url" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.influxdb.url") }}</label>
                    <div class="mt-1">
                      <input type="url" name="influxdb-url" v-model="influxdbURL" id="influxdb-url" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" placeholder="https://us-west-2-1.aws.cloud2.influxdata.com" />
                    </div>
                    <p class="mt-2 text-sm text-gray-500">{{ $t("cloud_config.influxdb.url_desc") }}</p>
                  </div>

                  <div class="col-span-6 lg:col-span-4">
                    <label for="influxdb-token" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.influxdb.token") }}</label>
                    <div class="mt-1">
                      <input type="password" name="influxdb-token" v-model="influxdbToken" id="influxdb-token" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                    </div>
                    <p class="mt-2 text-sm text-gray-500">{{ $t("cloud_config.influxdb.token_desc") }}</p>
                  </div>

                  <div class="col-span-6 lg:col-span-4">
                    <label for="influxdb-org" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.influxdb.organization") }}</label>
                    <div class="mt-1">
                      <input type="text" name="influxdb-org" v-model="influxdbOrg" id="influxdb-org" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                    </div>
                    <p class="mt-2 text-sm text-gray-500">{{ $t("cloud_config.influxdb.organization_desc") }}</p>
                  </div>

                  <div class="col-span-6 lg:col-span-4">
                    <label for="influxdb-bucket" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.influxdb.bucket") }}</label>
                    <div class="mt-1">
                      <input type="text" name="influxdb-bucket" v-model="influxdbBucket" id="influxdb-bucket" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                    </div>
                    <p class="mt-2 text-sm text-gray-500">{{ $t("cloud_config.influxdb.bucket_desc") }}</p>
                  </div>

                  <div class="col-span-6 lg:col-span-4">
                    <label for="influxdb-push-every" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.influxdb.push_frequency") }}</label>
                    <div class="mt-1">
                      <input type="number" name="influxdb-push-every" v-model="influxdbPushEvery" id="influxdb-push-every" min="60" max="86400" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                    </div>
                    <p class="mt-2 text-sm text-gray-500">{{ $t("cloud_config.influxdb.push_frequency_desc") }}</p>
                  </div>

                </div>
              </div>
            </div>

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
import UpdateSuccessfulModal from "@/components/config/UpdateSuccessfulModal.vue";
import {onMounted, ref} from "vue";
import { i18n } from "@/main.js";

const $loading = useLoading({
  // options
});

const updateSuccessful = ref(false);
const alertOpen = ref(false);
const configStore = useConfigStore();

let form_error_message = ref("");

// Destructured values from the config store
const influxdbURL = ref(configStore.influxdbURL);
const influxdbToken = ref(configStore.influxdbToken);
const influxdbOrg = ref(configStore.influxdbOrg);
const influxdbBucket = ref(configStore.influxdbBucket);
const influxdbPushEvery = ref(configStore.influxdbPushEvery);

function updateCachedSettings() {
  influxdbURL.value = configStore.influxdbURL;
  influxdbToken.value = configStore.influxdbToken;
  influxdbOrg.value = configStore.influxdbOrg;
  influxdbBucket.value = configStore.influxdbBucket;
  influxdbPushEvery.value = configStore.influxdbPushEvery;
}

let loader;

onMounted(() => {
  // Retrieve initial data
  if(configStore.loaded) {
    updateCachedSettings();
  } else {
    // Load the settings
    loader = $loading.show({});
    configStore.getConfig().then(() => {
      updateCachedSettings();
      loader.hide();
    }).catch(() => {
      loader.hide();
    });
  }
});

async function submitForm() {
  form_error_message.value = "";

  // Validation
  if (!influxdbURL.value || influxdbURL.value.trim() === "") {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_missing_url');
    return;
  }

  if (!influxdbToken.value || influxdbToken.value.trim() === "") {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_missing_token');
    return;
  }

  if (!influxdbOrg.value || influxdbOrg.value.trim() === "") {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_missing_org');
    return;
  }

  if (!influxdbBucket.value || influxdbBucket.value.trim() === "") {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_missing_bucket');
    return;
  }

  // URL validation
  try {
    new URL(influxdbURL.value);
  } catch (e) {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_invalid_url');
    return;
  }

  // Push frequency validation
  if (parseInt(influxdbPushEvery.value) < 60 || parseInt(influxdbPushEvery.value) > 86400) {
    form_error_message.value = i18n.global.t('cloud_config.influxdb.error_invalid_push_frequency');
    return;
  }

  // If there isn't a validation error, submit the form
  loader = $loading.show({});
  configStore.updateInfluxDBConfig(
      influxdbURL.value.trim(),
      influxdbToken.value.trim(),
      influxdbOrg.value.trim(),
      influxdbBucket.value.trim(),
      parseInt(influxdbPushEvery.value)
  ).then(() => {
    updateCachedSettings();
    updateSuccessful.value = !configStore.configUpdateError;  // configUpdateError is inverted from what we want here
    alertOpen.value = true;
  }).finally(() => {
    loader.hide();
  });
}

</script>

<style scoped>

</style>