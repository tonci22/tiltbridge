<template>
  <div class="rounded-md bg-red-50 p-4" v-if="formErrorMessage">
    <div class="flex">
      <div class="flex-shrink-0">
        <NoSymbolIcon class="h-5 w-5 text-red-400" aria-hidden="true" />
      </div>
      <div class="ml-3">
        <h3 class="text-sm font-medium text-red-800">
          {{ $t('sitewide.validation_error') }}
        </h3>
        <div class="mt-2 text-sm text-red-700">
          <p>
            {{ formErrorMessage }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { NoSymbolIcon } from "@heroicons/vue/20/solid";

defineProps({
  formErrorMessage: {
    type: String,
    required: true
  }
});
</script>