<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('device_config.general_settings_header') }}
          </h3>
        </div>

        <form @submit.prevent="submitForm">

          <div class="px-4 py-5">
            <!-- mDNS ID Field -->
            <TextField v-model="mdnsID" placeholder="tiltbridge">
              <template #FieldName>{{$t('device_config.mDNS_id_name') }}</template>
              <template #FieldDescription>{{$t('device_config.mDNS_id_desc') }}</template>
            </TextField>

            <!-- Time Zone Offset Field -->
            <!-- TODO - Move this to google sheets config -->
            <TextField v-model="TZoffset" placeholder="-5">
              <template #FieldName>{{$t('device_config.tz_offset_name') }}</template>
              <template #FieldDescription>{{$t('device_config.tz_offset_desc') }}</template>
            </TextField>


            <!-- Temperature Default Unit Selector -->
            <SelectField v-model="tempUnit">
              <template #FieldName>{{ $t('device_config.temp_unit_name') }}</template>
              <template #FieldDescription>{{ $t('device_config.temp_unit_desc') }}</template>
              <template #FieldOptions>
                <option value="C">{{ $t('sitewide.celsius') }}</option>
                <option value="F">{{ $t('sitewide.fahrenheit') }}</option>
              </template>
            </SelectField>


            <!-- Gravity Unit Selector -->
            <SelectField v-model="gravityUnit">
              <template #FieldName>{{ $t('device_config.gravity_unit_name') }}</template>
              <template #FieldDescription>{{ $t('device_config.gravity_unit_desc') }}</template>
              <template #FieldOptions>
                <option value="SG">{{ $t('sitewide.specific_gravity') }}</option>
                <option value="P">{{ $t('sitewide.plato') }}</option>
                <!-- <option value="B">{{ $t('sitewide.brix') }}</option> -->
              </template>
            </SelectField>


            <!-- Specific Gravity Averaging Field -->
            <TextField v-model="smoothFactor" placeholder="60">
              <template #FieldName>{{ $t('device_config.smooth_factor_name') }}</template>
              <template #FieldDescription>{{ $t('device_config.smooth_factor_desc') }}</template>
            </TextField>


            <!-- Invert TFT Selector -->
            <fieldset class="space-y-5" v-if="configStore.have_lcd">
              <legend class="sr-only">{{ $t('device_config.hardware_options_header') }}</legend>
              <CheckboxField v-model="invertTFT">
                <template #FieldName>{{ $t('device_config.invert_tft_name') }}</template>
                <template #FieldDescription>{{ $t('device_config.invert_tft_desc') }}</template>
              </CheckboxField>
            </fieldset>

            <!-- Combine Tilts Checkbox -->
            <fieldset class="space-y-5 mt-6">
              <CheckboxField v-model="combineTilts">
                <template #FieldName>{{ $t('device_config.combine_tilts_name') }}</template>
                <template #FieldDescription>{{ $t('device_config.combine_tilts_desc') }}</template>
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
import { useConfigStore } from "@/stores/ConfigStore";
import { useLoading } from 'vue-loading-overlay'
import TextField from "@/components/config/form_elements/TextField.vue";
import SelectField from "@/components/config/form_elements/SelectField.vue";
import CheckboxField from "@/components/config/form_elements/CheckboxField.vue";
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

let mdnsID = configStore.mdnsID;
let TZoffset = configStore.TZoffset;
let tempUnit = configStore.tempUnit;
let gravityUnit = configStore.gravityUnit;
let smoothFactor = configStore.smoothFactor;
let invertTFT = configStore.invertTFT;
let combineTilts = configStore.combineTilts;


function updateCachedSettings() {
  mdnsID = configStore.mdnsID;
  TZoffset = configStore.TZoffset;
  tempUnit = configStore.tempUnit;
  gravityUnit = configStore.gravityUnit;
  smoothFactor = configStore.smoothFactor;
  invertTFT = configStore.invertTFT;
  combineTilts = configStore.combineTilts;
}


async function submitForm() {
  // Validate the information in the form
  form_error_message.value = "";
  let mdns_re = new RegExp("^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$");

  if(!mdnsID.match(mdns_re)) {
    form_error_message.value = i18n.global.t('device_config.errors.mdns_invalid_chars');
    return;
  }

  if(mdnsID.length > 31 || mdnsID.length < 1) {
    form_error_message.value = i18n.global.t('device_config.errors.mdns_invalid_length');
    return;
  }

  if(parseInt(smoothFactor) > 99 || parseInt(smoothFactor) < 0) {
    form_error_message.value = i18n.global.t('device_config.errors.invalid_smoothing_factor');
    return;
  }

  if(parseInt(TZoffset) > 14 || parseInt(TZoffset) < -12) {
    form_error_message.value = i18n.global.t('device_config.errors.invalid_tz_offset');
    return;
  }

  // If there isn't a validation error, submit the form
  let loader = $loading.show({});
  configStore.updateDeviceConfig(
    mdnsID,
    parseInt(TZoffset),
    tempUnit,
    parseInt(smoothFactor),
    invertTFT,
    gravityUnit,
    combineTilts,
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