<template>
  <div>
    <label :for="uid" class="block text-sm font-medium text-gray-700"><slot name="FieldName"></slot></label>
    <div class="mt-1">
      <input type="text" :name="uid" v-model="value" :id="uid" class="shadow-sm focus:ring-indigo-500 focus:border-indigo-500 block w-full sm:text-sm border-gray-300 rounded-md" :placeholder="placeholder" :aria-describedby="uid + '-desc'" />
    </div>
    <p class="mt-1 mb-3 text-sm text-gray-500" :id="uid + '-desc'"><slot name="FieldDescription"></slot></p>
  </div>
</template>


<script setup>
import { computed, defineProps, onBeforeMount } from 'vue'

const props = defineProps({
  'modelValue': {
    required: true,
  },
  'placeholder': {
    type: String,
    required: false,
    default: "",
  }
});

const emit = defineEmits(['update:modelValue'])

const value = computed({
  get() {
    return props.modelValue
  },
  set(value) {
    emit('update:modelValue', value)
  }
})

let uid = 0;

// Before mounting, create a unique ID for this component and set uid equal to it
onBeforeMount(() => {
  uid = Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
})
</script>


<style scoped>

</style>