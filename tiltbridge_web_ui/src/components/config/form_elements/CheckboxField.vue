<template>
  <div>
    <div class="relative flex items-start">
      <div class="flex items-center h-5">
        <input :id="uid" v-model="value" :aria-describedby="uid + '-desc'" :name="uid" type="checkbox" class="focus:ring-indigo-500 h-4 w-4 text-indigo-600 border-gray-300 rounded" />
      </div>
      <div class="ml-3 text-sm">
        <label :for="uid" class="font-medium text-gray-700"><slot name="FieldName"></slot></label>
        <span :id="uid + '-desc'" class="text-gray-500 ml-3"><slot name="FieldDescription"></slot></span>
      </div>
    </div>
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