<template>
  <div>
    <SelectField v-model="selection">
      <template #FieldName><slot name="FieldName">{{ $t('cloud_config.push_interval.label') }}</slot></template>
      <template #FieldDescription><slot name="FieldDescription">{{ $t('cloud_config.push_interval.label_desc') }}</slot></template>
      <template #FieldOptions>
        <option v-for="seconds in presetSeconds" :key="seconds" :value="String(seconds)">{{ presetLabel(seconds) }}</option>
        <option value="custom">{{ $t('cloud_config.push_interval.custom_option') }}</option>
      </template>
    </SelectField>

    <div v-if="selection === 'custom'">
      <label :for="uid" class="block text-sm font-medium text-gray-700">{{ $t('cloud_config.push_interval.custom_interval') }}</label>
      <div class="mt-1">
        <input type="number" :name="uid" v-model="customMinutes" :id="uid" :min="minMinutes" :max="maxMinutes" step="1" class="shadow-sm focus:ring-indigo-500 focus:border-indigo-500 block w-full sm:text-sm border-gray-300 rounded-md" :aria-describedby="uid + '-desc'" />
      </div>
      <p class="mt-1 mb-3 text-sm text-gray-500" :id="uid + '-desc'">{{ $t('cloud_config.push_interval.custom_interval_desc', { min: minMinutes, max: maxMinutes }) }}</p>
    </div>
  </div>
</template>


<script setup>
/*
 * How often a reading is UPLOADED to one target.
 *
 * Deliberately not the same thing as the offline queue's snapshot interval on the Queue page:
 * that one controls how often queued readings are written to flash, so that a power cut loses
 * as little as possible, and it changes no upload schedule. Lowering one does not affect the
 * other, which is why they are configured in different places.
 *
 * The model value is in SECONDS, because that is what the firmware stores and serves. The
 * presets and the custom field are in MINUTES, because nobody thinks about an upload schedule
 * in seconds. All conversion happens here so no caller has to care.
 */
import { computed, ref, watch, onBeforeMount } from 'vue'
import SelectField from '@/components/config/form_elements/SelectField.vue'

const PRESET_MINUTES = [10, 15, 30, 45, 60, 120, 180, 240];

const props = defineProps({
  'modelValue': {
    required: true,
  },
  /* Seconds. The firmware clamps to the same bounds and refuses anything outside them. */
  'min': {
    type: Number,
    required: false,
    default: 600,
  },
  'max': {
    type: Number,
    required: false,
    default: 43200,
  }
});

const emit = defineEmits(['update:modelValue'])

const minMinutes = computed(() => Math.ceil(props.min / 60));
const maxMinutes = computed(() => Math.floor(props.max / 60));

/* Only offer presets the firmware would actually accept. */
const presetSeconds = computed(() =>
  PRESET_MINUTES.map((m) => m * 60).filter((s) => s >= props.min && s <= props.max)
);

function presetLabel(seconds) {
  const minutes = seconds / 60;
  if (minutes < 60) return $t('cloud_config.push_interval.minutes_option', { minutes });
  if (minutes === 60) return $t('cloud_config.push_interval.one_hour_option');
  return $t('cloud_config.push_interval.hours_option', { hours: minutes / 60 });
}

/*
 * $t is not auto-injected into <script setup>, only into the template, so pull the same
 * translator the rest of the app uses.
 */
import { i18n } from '@/main'
const $t = (key, params) => i18n.global.t(key, params || {});

const selection = ref('');
const customMinutes = ref('');

/*
 * A stored value that is not one of the presets - an older config, or something typed by hand -
 * opens as "Custom..." with that value, rather than being silently rounded to a preset.
 */
function syncFromModel() {
  const seconds = Number(props.modelValue);
  if (!Number.isFinite(seconds) || seconds <= 0) return;

  const minutes = Math.round(seconds / 60);
  customMinutes.value = String(minutes);
  selection.value = presetSeconds.value.includes(seconds) ? String(seconds) : 'custom';
}

function currentSeconds() {
  if (selection.value === 'custom') {
    const minutes = parseInt(customMinutes.value, 10);
    return Number.isFinite(minutes) ? minutes * 60 : Number(props.modelValue);
  }
  return parseInt(selection.value, 10);
}

onBeforeMount(syncFromModel);

/* Re-seed when the parent reloads it from the config store. */
watch(() => props.modelValue, (incoming) => {
  if (Number(incoming) !== currentSeconds()) syncFromModel();
});

/* Guarded so re-seeding from the model above cannot bounce an update back out. */
watch([selection, customMinutes], () => {
  const seconds = currentSeconds();
  if (Number.isFinite(seconds) && seconds !== Number(props.modelValue)) {
    emit('update:modelValue', seconds);
  }
});

let uid = 0;

onBeforeMount(() => {
  uid = Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
})
</script>


<style scoped>

</style>
