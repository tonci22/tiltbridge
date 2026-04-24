<template>
  <div>
    <label :for="uid" class="block text-sm font-medium text-gray-700"><slot name="FieldName"></slot></label>
    <div class="mt-1">
      <select :id="uid" v-model="value" :name="uid" class="focus:ring-indigo-500 focus:border-indigo-500 relative block w-full rounded-md  bg-transparent focus:z-10 sm:text-sm border-gray-300" :aria-describedby="uid + '-desc'">
        <slot name="FieldOptions"></slot>
      </select>
    </div>
    <p class="mt-1 mb-3 text-sm text-gray-500" :id="uid + '-desc'"><slot name="FieldDescription"></slot></p>
  </div>
</template>


<script setup>
import { computed, defineProps, onBeforeMount } from 'vue'

const props = defineProps({
  'modelValue': {
    required: true,
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