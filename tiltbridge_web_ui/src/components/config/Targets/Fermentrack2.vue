<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('cloud_config.fermentrack.header') }}
          </h3>
        </div>

        <div class="px-4 sm:px-6 pt-4">
          <SendTargetErrorMsg target-key="fermentrack" />
        </div>

        <form @submit.prevent="submitForm">

          <div :class="[selectedSettingSet.value === 0 ? 'pt-5 pb-72' : 'py-5', 'px-4']">

            <Listbox as="div" v-model="selectedSettingSet">
              <ListboxLabel class="sr-only">{{ $t('cloud_config.fermentrack.fermentrack_settings_set_sr') }}</ListboxLabel>

              <div class="relative mt-2">
                <ListboxButton class="grid w-full cursor-default grid-cols-1 rounded-md bg-white py-1.5 pl-3 pr-2 text-left text-gray-900 outline outline-1 -outline-offset-1 outline-gray-300 focus:outline focus:outline-2 focus:-outline-offset-2 focus:outline-indigo-600 sm:text-sm/6">
                  <span class="col-start-1 row-start-1 truncate pr-6">{{ selectedSettingSet.title }}</span>
                  <ChevronUpDownIcon class="col-start-1 row-start-1 size-5 self-center justify-self-end text-gray-500 sm:size-4" aria-hidden="true" />
                </ListboxButton>

                <transition leave-active-class="transition ease-in duration-100" leave-from-class="opacity-100" leave-to-class="opacity-0">
                  <ListboxOptions class="absolute z-10 mt-1 max-h-60 w-full overflow-auto rounded-md bg-white py-1 text-base shadow-lg ring-1 ring-black/5 focus:outline-none sm:text-sm">
                    <ListboxOption as="template" v-for="option in upstreamSettingsSets" :key="option.title" :value="option" v-slot="{ active, selected }">
                      <li :class="[active ? 'bg-indigo-600 text-white' : 'text-gray-900', 'cursor-default select-none p-4 text-sm']">
                        <!--                      <li :class="[active ? 'bg-indigo-600 text-white outline-none' : 'text-gray-900', 'relative cursor-default select-none py-2 pl-3 pr-9']">-->
                        <!--                        <span :class="[selected ? 'font-semibold' : 'font-normal', 'block truncate']">{{ option.title }}</span>-->

                        <!--                        <span v-if="selected" :class="[active ? 'text-white' : 'text-indigo-600', 'absolute inset-y-0 right-0 flex items-center pr-4']">-->
                        <!--                          <CheckIcon class="size-5" aria-hidden="true" />-->
                        <!--                        </span>-->
                        <!--                      </li>-->
                        <div class="flex flex-col">
                          <div class="flex justify-between">
                            <p :class="selected ? 'font-semibold' : 'font-normal'">{{ option.title }}</p>
                            <span v-if="selected" :class="active ? 'text-white' : 'text-indigo-600'">
                                  <CheckIcon class="h-5 w-5" aria-hidden="true" />
                                </span>
                          </div>
                          <p :class="[active ? 'text-indigo-200' : 'text-gray-500', 'mt-2']">{{ option.description }}</p>
                        </div>
                      </li>
                    </ListboxOption>
                  </ListboxOptions>
                </transition>
              </div>


<!--              <div class="relative mt-2">-->
<!--                <div class="inline-flex divide-x divide-indigo-700 rounded-md shadow-sm outline-none">-->
<!--                  <div class="inline-flex items-center gap-x-1.5 rounded-l-md bg-indigo-600 py-2 px-3 text-white shadow-sm">-->
<!--                    <CheckIcon class="-ml-0.5 size-5" aria-hidden="true" />-->
<!--                    <p class="text-sm font-semibold">{{ selectedSettingSet.title }}</p>-->
<!--                  </div>-->
<!--                  <ListboxButton class="inline-flex items-center rounded-l-none rounded-r-md bg-indigo-600 p-2 hover:bg-indigo-700 focus:outline-none focus:ring-2 focus:ring-indigo-600 focus:ring-offset-2 focus:ring-offset-gray-50">-->
<!--                    <span class="sr-only">{{ $t('cloud_config.fermentrack.fermentrack_settings_set_sr') }}</span>-->
<!--                    <ChevronDownIcon class="size-5 text-white forced-colors:text-[Highlight]" aria-hidden="true" />-->
<!--                  </ListboxButton>-->
<!--                </div>-->

<!--                <transition leave-active-class="transition ease-in duration-100" leave-from-class="opacity-100" leave-to-class="opacity-0">-->
<!--                  <ListboxOptions class="absolute right-0 z-10 mt-2 w-72 origin-top-right divide-y divide-gray-200 overflow-hidden rounded-md bg-white shadow-lg ring-1 ring-black/5 focus:outline-none">-->
<!--                    <ListboxOption as="template" v-for="option in upstreamSettingsSets" :key="option.title" :value="option" v-slot="{ active, selected }">-->
<!--                      <li :class="[active ? 'bg-indigo-600 text-white' : 'text-gray-900', 'cursor-default select-none p-4 text-sm']">-->
<!--                        <div class="flex flex-col">-->
<!--                          <div class="flex justify-between">-->
<!--                            <p :class="selected ? 'font-semibold' : 'font-normal'">{{ option.title }}</p>-->
<!--                            <span v-if="selected" :class="active ? 'text-white' : 'text-indigo-600'">-->
<!--                                  <CheckIcon class="class-5" aria-hidden="true" />-->
<!--                                </span>-->
<!--                          </div>-->
<!--                          <p :class="[active ? 'text-indigo-200' : 'text-gray-500', 'mt-2']">{{ option.description }}</p>-->
<!--                        </div>-->
<!--                      </li>-->
<!--                    </ListboxOption>-->
<!--                  </ListboxOptions>-->
<!--                </transition>-->

<!--              </div>-->
            </Listbox>



            <div class="space-y-8 divide-y divide-gray-200" v-if="selectedSettingSet.value !== 0">
              <div>
                <!-- Display errors from the upstream registration process -->
                <div v-if="configStore.loaded && configStore.fermentrackHostname !== '' && (configStore.fermentrackUser !== '' || configStore.fermentrackAPIKey !== '' || configStore.fermentrackDeviceID !== '')">
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-if="configStore.fermentrackRegistrationError === 1 || configStore.fermentrackRegistrationError === 5 || configStore.fermentrackRegistrationError === 6">
                    <!-- Error 1 (missing GUID), Error 5 (missing hardware), Error 6 (missing firmware version) are all internal -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.missing_data_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else-if="configStore.fermentrackRegistrationError === 3">
                    <!-- Error 3 - User not found -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.invalid_username_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else-if="configStore.fermentrackRegistrationError === 7">
                    <!-- Error 7 - API Key is not associated with a brewhouse -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.bad_api_key_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else-if="configStore.fermentrackRegistrationError === 4">
                    <!-- Error 4 - User does not have a brewhouse -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.missing_brewhouse_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else-if="configStore.fermentrackRegistrationError === 2 || (configStore.fermentrackUser === '' && configStore.fermentrackAPIKey === '')">
                    <!-- Error 2 - Missing username or API key -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.no_username_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-yellow-400 bg-yellow-50 p-4 mt-4 mb-4" v-else-if="configStore.fermentrackRegistrationError === 8">
                    <!-- Error 2 - Missing username or API key -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-yellow-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-yellow-700">
                          {{ $t("cloud_config.fermentrack.errors.waiting_to_register_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else-if="configStore.fermentrackRegistrationError === 9">
                    <!-- Error 9 - Registration endpoint error -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.unable_to_reach_fermentrack_error") }}
                        </p>
                      </div>
                    </div>
                  </div>

                  <div v-else-if="configStore.fermentrackRegistrationError === 0">
                    <div class="border-l-4 border-green-400 bg-green-50 p-4 mb-4 mt-4">
                      <!-- Error 0 - No error -->
                      <div class="flex">
                        <div class="flex-shrink-0">
                          <InformationCircleIcon class="h-5 w-5 text-green-400" aria-hidden="true" />
                        </div>
                        <div class="ml-3">
                          <p class="text-sm text-green-700">
                            {{ $t("cloud_config.fermentrack.errors.successful_registration_msg") }}
                          </p>
                        </div>
                      </div>
                    </div>

                  </div>

                  <div class="border-l-4 border-red-400 bg-red-50 p-4 mb-4 mt-4" v-else>
                    <!-- Error code not captured. Means that Fermentrack is probably more recent than this firmware -->
                    <div class="flex">
                      <div class="flex-shrink-0">
                        <ExclamationTriangleIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
                      </div>
                      <div class="ml-3">
                        <p class="text-sm text-red-700">
                          {{ $t("cloud_config.fermentrack.errors.unknown_error") }}
                        </p>
                      </div>
                    </div>
                  </div>
                </div>

                <div class="mt-6 grid grid-cols-1 gap-y-6 gap-x-4 sm:grid-cols-6">
                  <!-- Hide the below unless the user is using a custom upstream -->
                  <div class="col-span-6 lg:col-span-4">
                    <label for="hostname" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.fermentrack.hostname") }}</label>
                    <div class="mt-1">
                      <!-- Note - There is no actual differentiation between HTTPS and HTTP as for now the controller currently only can use HTTP for outgoing connections -->
                      <!-- fermentrackRegistrationError !== 8 is a check to see if the TiltBridge is waiting to register -->
                      <div v-if="configStore.fermentrackDeviceID === '' && selectedSettingSet.value !== 1" class="flex rounded-md shadow-sm">
                        <span class="inline-flex items-center rounded-l-md border border-r-0 border-gray-300 bg-gray-50 px-3 text-gray-500 sm:text-sm" v-if="fermentrackPort.value === 443">http://</span>
                        <span class="inline-flex items-center rounded-l-md border border-r-0 border-gray-300 bg-gray-50 px-3 text-gray-500 sm:text-sm" v-else>http://</span>
                        <input type="text" name="hostname" v-model="fermentrackHostname"  id="hostname" class="block w-full min-w-0 flex-1 rounded-none rounded-r-md border-gray-300 focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                      </div>
                      <span v-else-if="selectedSettingSet.value === 1">
                        <!-- For Fermentrack.net, we are currently hardcoding to port 80. If I add HTTPS support and change to 443, then this will become valid -->
                        <span v-if="ft_net_port === 443">https://</span><span v-else>http://</span>{{ ft_net_host }}
                      </span>
                      <span v-else>
                        <!-- Effectively only shows when selectedSettingSet.value === 2 -->
                        <!-- Note - There is no actual differentiation between HTTPS and HTTP as for now the controller currently only can use HTTP for outgoing connections -->
                        <span v-if="configStore.fermentrackPort === 443">http://</span><span v-else>http://</span>{{ fermentrackHostname }}
                      </span>
                    </div>
                  </div>

                  <!-- Hide the below unless the user is using a custom upstream -->
                  <div class="col-span-6 lg:col-span-4">
                    <label for="port" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.fermentrack.port") }}</label>
                    <div class="mt-1">
                      <input id="port" name="port" v-model="fermentrackPort" v-if="configStore.fermentrackDeviceID === '' && selectedSettingSet.value !== 1" type="text" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                      <span v-else-if="selectedSettingSet.value === 1">{{ ft_net_port }}</span>
                      <span v-else>{{ fermentrackPort }}</span>
                    </div>
                  </div>

                  <!-- Hide the below when the user is either already registered, or is using an upstream that doesn't require a username -->
                  <div class="col-span-6 lg:col-span-4" v-if="configStore.fermentrackDeviceID === ''">
                    <label for="username" class="block text-sm font-medium text-gray-700">
                      <span v-if="selectedSettingSet.value === 1">{{ $t("cloud_config.fermentrack.ft_net_username") }}</span>
                      <span v-else>{{ $t("cloud_config.fermentrack.username") }}</span>
                    </label>
                    <div class="mt-1">
                      <input type="text" name="username" v-model="fermentrackUser" id="username" data-form-type="other" class="block w-full rounded-md border-gray-300 shadow-sm focus:border-indigo-500 focus:ring-indigo-500 sm:text-sm" />
                    </div>
                  </div>


                  <div class="col-span-6 lg:col-span-4">
                    <PushIntervalField v-model="fermentrackPushEvery">
                      <template #FieldName>{{ $t("cloud_config.fermentrack.ft2_push_frequency") }}</template>
                      <template #FieldDescription>{{ $t("cloud_config.fermentrack.ft2_push_frequency_desc") }}</template>
                    </PushIntervalField>
                  </div>


                  <div class="col-span-6 lg:col-span-4" v-if="configStore.fermentrackHostname !== ''">
                    <label for="deviceid" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.fermentrack.device_id") }}</label>
                    <div class="mt-1">
                      <span v-if="configStore.fermentrackDeviceID.length <= 0">{{ $t("cloud_config.fermentrack.not_yet_registered") }}</span>
                      <span v-else>{{ configStore.fermentrackDeviceID }}</span>
                    </div>
                  </div>

                  <!-- Always display the Hardware GUID, as it is part of what is registered with Fermentrack -->
                  <div class="col-span-6 lg:col-span-4">
                    <label for="deviceid" class="block text-sm font-medium text-gray-700">{{ $t("cloud_config.fermentrack.hardware_guid") }}</label>
                    <div class="mt-1">
                      <span>{{ configStore.guid }}</span>
                    </div>
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
import PushIntervalField from "@/components/config/form_elements/PushIntervalField.vue";
import {onMounted, ref} from "vue";
import { i18n } from "@/main.js";
import { Listbox, ListboxButton, ListboxLabel, ListboxOption, ListboxOptions } from "@headlessui/vue";
import {CheckIcon, ChevronDownIcon, ChevronUpDownIcon } from "@heroicons/vue/20/solid";
import { ExclamationTriangleIcon, InformationCircleIcon } from "@heroicons/vue/24/outline";

// selectedSettingSet is one of three values:
//   0 - "No Fermentack" (or legacy FT target)
//   1 - Fermentrack.net
//   2 - Local Fermentrack 2 (FT2, but not FT.net)
// The determination of which is currently in use is made in the onMounted hook by comparing the value to the defined
// FT.net values
const upstreamSettingsSets = [
  { title: i18n.global.t('cloud_config.fermentrack.presets.no_fermentrack'), description: i18n.global.t('cloud_config.fermentrack.presets.no_fermentrack_desc'), value: 0 },
  { title: i18n.global.t('cloud_config.fermentrack.presets.fermentrack_net'), description: i18n.global.t('cloud_config.fermentrack.presets.fermentrack_net_desc'), value: 1 },
  { title: i18n.global.t('cloud_config.fermentrack.presets.fermentrack_local'), description: i18n.global.t('cloud_config.fermentrack.presets.fermentrack_local_desc'), value: 2 },
]

let selectedSettingSet = ref(upstreamSettingsSets[0]);


const $loading = useLoading({
  // options
});

const updateSuccessful = ref(false);
const alertOpen = ref(false);
const configStore = useConfigStore();

let form_error_message = ref("");

// Destructured values from the config store
const fermentrackHostname = ref(configStore.fermentrackHostname);
const fermentrackPort = ref(configStore.fermentrackPort);
const fermentrackUser = ref(configStore.fermentrackUser);
const fermentrackDeviceID = ref(configStore.fermentrackDeviceID);
const fermentrackPushEvery = ref(configStore.fermentrackPushEvery);

// Hardcoded Fermentrack.net values
const ft_net_host = "www.fermentrack.net";
const ft_net_port = 80;


function updateCachedSettings() {
  fermentrackHostname.value = configStore.fermentrackHostname;
  fermentrackPort.value = configStore.fermentrackPort;
  fermentrackUser.value = configStore.fermentrackUser;
  fermentrackDeviceID.value = configStore.fermentrackDeviceID;
  fermentrackPushEvery.value = configStore.fermentrackPushEvery;
}


function setInitialSelectedSettingSet() {
  if(fermentrackHostname.value === ft_net_host && fermentrackPort.value === ft_net_port) {
    // Fermentrack.net is selected
    selectedSettingSet.value = upstreamSettingsSets[1];
  } else if(fermentrackHostname.value !== "") {
    // Local Fermentrack 2 is selected
    selectedSettingSet.value = upstreamSettingsSets[2];
  } else {
    selectedSettingSet.value = upstreamSettingsSets[0];
  }
}


let loader;

onMounted(() => {
  // Retrieve initial data
  if(configStore.loaded) {
    updateCachedSettings();
    setInitialSelectedSettingSet();
  } else {
    // Load the settings
    loader = $loading.show({});
    configStore.getConfig().then(() => {
      updateCachedSettings();
      loader.hide();
      setInitialSelectedSettingSet();
    }).catch(() => {
      loader.hide();
    });
  }
});

async function submitForm() {
  form_error_message.value = "";

  // selectedSettingSet.value is the value of the selected setting set (including title, description, and value)
  // We only want the numeric value, hence value.value here
  if(selectedSettingSet.value.value === 0) {
    // For no Fermentrack, clear the values
    fermentrackHostname.value = "";
    fermentrackPort.value = 80;
    fermentrackUser.value = "";
  } else if(selectedSettingSet.value.value === 1) {
    // For Fermentrack.net, overwrite the hostname/port with the hardcoded values
    fermentrackHostname.value = ft_net_host;
    fermentrackPort.value = ft_net_port;
  }


  // TODO - Add some kind of "disconnect from Fermentrack" option
  if(parseInt(fermentrackPort.value) > 65535 || parseInt(fermentrackPort.value) < 10) {
    form_error_message.value = i18n.global.t('cloud_config.fermentrack.errors.error_port_out_of_range');
    return;
  }

  const pushEvery = parseInt(fermentrackPushEvery.value);

  if(!Number.isFinite(pushEvery) || pushEvery > 43200 || pushEvery < 600) {
    form_error_message.value = i18n.global.t('cloud_config.fermentrack.push_frequency_out_of_range', { min: 600, max: 43200 });
    return;
  }

  /*
   * Posting the connection details is what makes the firmware forget its device id and API key
   * and register again. Changing only how often readings are uploaded is no reason to do that,
   * so when the connection is untouched the interval goes up on its own.
   */
  const connectionUnchanged =
      fermentrackHostname.value === configStore.fermentrackHostname &&
      parseInt(fermentrackPort.value) === configStore.fermentrackPort &&
      fermentrackUser.value === configStore.fermentrackUser;

  // If there isn't a validation error, submit the form
  loader = $loading.show({});

  const saved = connectionUnchanged
      ? configStore.updateFermentrackPushEvery(pushEvery)
      : configStore.updateFermentrackConfig(
          fermentrackHostname.value,
          parseInt(fermentrackPort.value),
          fermentrackUser.value,
          pushEvery
        );

  saved.then(() => {
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