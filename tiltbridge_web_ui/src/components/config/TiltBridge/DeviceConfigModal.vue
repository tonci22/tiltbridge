<template>
  <TransitionRoot as="template" :show="modalOpen">
    <Dialog as="div" class="fixed z-10 inset-0 overflow-y-auto" @close="closeModal">
      <div class="flex items-end justify-center min-h-screen pt-4 px-4 pb-20 text-center sm:block sm:p-0">
        <TransitionChild as="template" enter="ease-out duration-300" enter-from="opacity-0" enter-to="opacity-100" leave="ease-in duration-200" leave-from="opacity-100" leave-to="opacity-0">
          <DialogOverlay class="fixed inset-0 bg-gray-500 bg-opacity-75 transition-opacity" />
        </TransitionChild>

        <!-- This element is to trick the browser into centering the modal contents. -->
        <span class="hidden sm:inline-block sm:align-middle sm:h-screen" aria-hidden="true">&#8203;</span>
        <TransitionChild as="template" enter="ease-out duration-300" enter-from="opacity-0 translate-y-4 sm:translate-y-0 sm:scale-95" enter-to="opacity-100 translate-y-0 sm:scale-100" leave="ease-in duration-200" leave-from="opacity-100 translate-y-0 sm:scale-100" leave-to="opacity-0 translate-y-4 sm:translate-y-0 sm:scale-95">
          <div class="inline-block align-bottom bg-white rounded-lg px-4 pt-5 pb-4 text-left overflow-hidden shadow-xl transform transition-all sm:my-8 sm:align-middle sm:max-w-lg sm:w-full sm:p-6">

            <DialogTitle as="h3" class="text-lg leading-6 font-medium text-gray-900">
              {{ $t('tilt_device_config.header') }}
            </DialogTitle>
            <p class="mt-1 text-sm text-gray-500">
              {{ $t('tilt_device_config.description') }}
            </p>

            <!-- Read-only identity. This is what the settings below are bound to. -->
            <dl class="mt-4 bg-gray-50 rounded-lg px-4 py-3 grid grid-cols-1 gap-2 sm:grid-cols-3">
              <div>
                <dt class="text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_device_config.mac_label') }}</dt>
                <dd class="text-sm font-mono text-gray-900 break-all">{{ deviceId || $t('sitewide.unknown') }}</dd>
              </div>
              <div>
                <dt class="text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_device_config.detected_color_label') }}</dt>
                <dd class="text-sm text-gray-900">{{ detectedColorName }}</dd>
              </div>
              <div>
                <dt class="text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('tilt_device_config.detected_model_label') }}</dt>
                <dd class="text-sm text-gray-900">{{ detectedModel || $t('sitewide.unknown') }}</dd>
              </div>
            </dl>

            <form @submit.prevent="submitForm">
              <div class="mt-4 max-h-[55vh] overflow-y-auto pr-1">

                <!-- Friendly Name -->
                <TextField v-model="friendlyName" :placeholder="detectedColorName">
                  <template #FieldName>{{ $t('tilt_device_config.friendly_name') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.friendly_name_desc') }}</template>
                </TextField>

                <!-- Google Sheets Name -->
                <TextField v-model="googleSheetsName" placeholder="BeerName">
                  <template #FieldName>{{ $t('tilt_device_config.gsheets_name') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.gsheets_name_desc') }}</template>
                </TextField>

                <!-- Manual Model Label -->
                <TextField v-model="modelLabel" :placeholder="detectedModel">
                  <template #FieldName>{{ $t('tilt_device_config.model_label') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.model_label_desc') }}</template>
                </TextField>

                <!-- Notes -->
                <TextField v-model="notes" :placeholder="$t('tilt_device_config.notes_placeholder')">
                  <template #FieldName>{{ $t('tilt_device_config.notes') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.notes_desc') }}</template>
                </TextField>

                <!-- Repeater Aliases -->
                <TextField v-model="alias1" placeholder="88:C2:55:AC:26:81">
                  <template #FieldName>{{ $t('tilt_device_config.alias_one') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.alias_desc') }}</template>
                </TextField>

                <TextField v-model="alias2" placeholder="88:C2:55:AC:26:81">
                  <template #FieldName>{{ $t('tilt_device_config.alias_two') }}</template>
                  <template #FieldDescription>{{ $t('tilt_device_config.alias_desc') }}</template>
                </TextField>

                <!-- Enabled -->
                <fieldset class="space-y-5 mt-2 mb-4">
                  <CheckboxField v-model="enabled">
                    <template #FieldName>{{ $t('tilt_device_config.enabled') }}</template>
                    <template #FieldDescription>{{ $t('tilt_device_config.enabled_desc') }}</template>
                  </CheckboxField>
                </fieldset>

                <!-- Calibration link -->
                <div class="rounded-md bg-gray-50 px-4 py-3 mb-4">
                  <button type="button" class="text-sm font-medium text-indigo-600 hover:text-indigo-800" @click="goToCalibration">
                    {{ $t('tilt_device_config.calibration_link') }}
                  </button>
                  <p class="mt-1 text-sm text-gray-500">{{ $t('tilt_device_config.calibration_link_desc') }}</p>
                </div>

                <!-- Reset to colour defaults -->
                <div class="rounded-md bg-orange-50 px-4 py-3 mb-2">
                  <h4 class="text-sm font-medium text-orange-800">{{ $t('tilt_device_config.reset_header') }}</h4>
                  <p class="mt-1 text-sm text-orange-700">{{ $t('tilt_device_config.reset_desc') }}</p>
                  <div class="mt-3">
                    <button v-if="!resetArmed" type="button" :disabled="!hasDeviceRecord" class="inline-flex items-center px-3 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-orange-600 hover:bg-orange-700 disabled:bg-gray-400 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-orange-500" @click="resetArmed = true">
                      {{ $t('tilt_device_config.reset_button') }}
                    </button>
                    <div v-else class="flex gap-3">
                      <button type="button" class="inline-flex items-center px-3 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-orange-600 hover:bg-orange-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-orange-500" @click="resetToColorDefaults">
                        {{ $t('tilt_device_config.reset_confirm_button') }}
                      </button>
                      <button type="button" class="inline-flex items-center px-3 py-2 border border-gray-300 text-sm font-medium rounded-md text-gray-700 bg-white hover:bg-gray-50" @click="resetArmed = false">
                        {{ $t('sitewide.cancel') }}
                      </button>
                    </div>
                  </div>
                </div>

                <FormErrorMsg :form-error-message="form_error_message" v-if="form_error_message.length > 0" />
              </div>

              <div class="mt-5 sm:mt-6 sm:flex sm:flex-row-reverse gap-3">
                <button type="submit" class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-blue-600 text-base font-medium text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 sm:w-auto sm:text-sm">
                  {{ $t('sitewide.save') }}
                </button>
                <button type="button" class="mt-3 sm:mt-0 w-full inline-flex justify-center rounded-md border border-gray-300 shadow-sm px-4 py-2 bg-white text-base font-medium text-gray-700 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500 sm:w-auto sm:text-sm" @click="closeModal">
                  {{ $t('sitewide.cancel') }}
                </button>
              </div>
            </form>

          </div>
        </TransitionChild>
      </div>
    </Dialog>
  </TransitionRoot>
</template>

<script setup>
import { computed, ref, watch } from 'vue';
import { useRouter } from 'vue-router';
import { Dialog, DialogOverlay, DialogTitle, TransitionChild, TransitionRoot } from "@headlessui/vue";
import TextField from "@/components/config/form_elements/TextField.vue";
import CheckboxField from "@/components/config/form_elements/CheckboxField.vue";
import FormErrorMsg from "@/components/generic/FormErrorMsg.vue";
import { useDeviceConfigStore } from "@/stores/DeviceConfigStore";
import { isValidMac } from "@/mixins/TiltDevice";
import { i18n } from "@/main.js";

const props = defineProps({
  'modelValue': {
    type: Boolean,
    required: true,
  },
  // The TiltDevice this modal is configuring. Supplies the read-only identity block and the
  // colour index the firmware needs on an upsert.
  'tilt': {
    required: false,
    default: null,
  },
});

const emit = defineEmits(['update:modelValue', 'saved']);

const router = useRouter();
const deviceConfigStore = useDeviceConfigStore();

const friendlyName = ref("");
const googleSheetsName = ref("");
const modelLabel = ref("");
const notes = ref("");
const alias1 = ref("");
const alias2 = ref("");
const enabled = ref(true);
const resetArmed = ref(false);
const form_error_message = ref("");

const modalOpen = computed({
  get() {
    return props.modelValue;
  },
  set(value) {
    emit('update:modelValue', value);
  }
});

const deviceId = computed(() => (props.tilt ? props.tilt.deviceId : ""));
const detectedColorName = computed(() => {
  if (!props.tilt || !props.tilt.color) return i18n.global.t('sitewide.unknown');
  const key = `sitewide.tilt_colors.${props.tilt.color.toLowerCase()}`;
  return i18n.global.te(key) ? i18n.global.t(key) : props.tilt.color;
});
const detectedModel = computed(() => (props.tilt ? props.tilt.modelLabel : ""));
const hasDeviceRecord = computed(() => !!deviceConfigStore.findDevice(deviceId.value));

async function loadForm() {
  form_error_message.value = "";
  resetArmed.value = false;

  // Always re-read: another tab (or the firmware itself) may have changed the table.
  await deviceConfigStore.getDevices();

  const record = deviceConfigStore.findDevice(deviceId.value);
  if (record) {
    friendlyName.value = record.friendlyName || "";
    googleSheetsName.value = record.googleSheetsName || "";
    modelLabel.value = record.modelLabel || "";
    notes.value = record.notes || "";
    enabled.value = record.enabled !== false;
    const aliases = record.aliases || [];
    alias1.value = aliases[0] || "";
    alias2.value = aliases[1] || "";
  } else {
    // No stored record yet - this Tilt is still on the shared colour configuration.
    friendlyName.value = "";
    googleSheetsName.value = props.tilt ? (props.tilt.gsheets_name || "") : "";
    modelLabel.value = "";
    notes.value = "";
    enabled.value = props.tilt ? props.tilt.enabled !== false : true;
    alias1.value = "";
    alias2.value = "";
  }
}

watch(() => props.modelValue, (isOpen) => {
  if (isOpen) {
    loadForm();
  }
});

function closeModal() {
  modalOpen.value = false;
}

function goToCalibration() {
  const target = props.tilt ? props.tilt.calibrationTarget : "";
  if (!target) return;
  closeModal();
  router.push(`/calibrate/${target}/`);
}

function validate() {
  form_error_message.value = "";

  if (!deviceId.value) {
    form_error_message.value = i18n.global.t('tilt_device_config.errors.missing_device_id');
    return false;
  }
  if (friendlyName.value.length > 32) {
    form_error_message.value = i18n.global.t('tilt_device_config.errors.friendly_name_too_long');
    return false;
  }
  if (googleSheetsName.value.length > 25) {
    form_error_message.value = i18n.global.t('tilt_device_config.errors.gsheets_name_too_long');
    return false;
  }
  if (modelLabel.value.length > 16) {
    form_error_message.value = i18n.global.t('tilt_device_config.errors.model_label_too_long');
    return false;
  }
  if (notes.value.length > 64) {
    form_error_message.value = i18n.global.t('tilt_device_config.errors.notes_too_long');
    return false;
  }
  for (const alias of [alias1.value, alias2.value]) {
    if (alias.trim().length > 0 && !isValidMac(alias)) {
      form_error_message.value = i18n.global.t('tilt_device_config.errors.invalid_alias');
      return false;
    }
  }
  return true;
}

async function submitForm() {
  if (!validate()) return;

  const aliases = [alias1.value, alias2.value]
      .map((a) => a.trim().toUpperCase())
      .filter((a) => a.length > 0);

  const payload = {
    deviceId: deviceId.value,
    friendlyName: friendlyName.value,
    googleSheetsName: googleSheetsName.value,
    modelLabel: modelLabel.value,
    notes: notes.value,
    enabled: enabled.value,
    aliases: aliases,
  };

  // colorIndex lets the firmware create the record with the right colour fallback when this
  // is the first time the Tilt has been configured.
  if (props.tilt && props.tilt.colorIndex >= 0) {
    payload.colorIndex = props.tilt.colorIndex;
  }

  const success = await deviceConfigStore.saveDevice(payload);
  closeModal();
  emit('saved', success);
}

async function resetToColorDefaults() {
  resetArmed.value = false;
  const success = await deviceConfigStore.resetDeviceToColorDefaults(deviceId.value);
  closeModal();
  emit('saved', success);
}
</script>

<style scoped>

</style>
