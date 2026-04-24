<template>
  <TransitionRoot as="template" :show="alertOpen">
    <Dialog as="div" class="fixed z-10 inset-0 overflow-y-auto" @close="alertOpen = false">
      <div class="flex items-end justify-center min-h-screen pt-4 px-4 pb-20 text-center sm:block sm:p-0">
        <TransitionChild as="template" enter="ease-out duration-300" enter-from="opacity-0" enter-to="opacity-100" leave="ease-in duration-200" leave-from="opacity-100" leave-to="opacity-0">
          <DialogOverlay class="fixed inset-0 bg-gray-500 bg-opacity-75 transition-opacity" />
        </TransitionChild>

        <!-- This element is to trick the browser into centering the modal contents. -->
        <span class="hidden sm:inline-block sm:align-middle sm:h-screen" aria-hidden="true">&#8203;</span>
        <TransitionChild as="template" enter="ease-out duration-300" enter-from="opacity-0 translate-y-4 sm:translate-y-0 sm:scale-95" enter-to="opacity-100 translate-y-0 sm:scale-100" leave="ease-in duration-200" leave-from="opacity-100 translate-y-0 sm:scale-100" leave-to="opacity-0 translate-y-4 sm:translate-y-0 sm:scale-95">
          <div class="inline-block align-bottom bg-white rounded-lg px-4 pt-5 pb-4 text-left overflow-hidden shadow-xl transform transition-all sm:my-8 sm:align-middle sm:max-w-sm sm:w-full sm:p-6">
            <div v-if="updateSuccessful">
              <div class="mx-auto flex items-center justify-center h-12 w-12 rounded-full bg-green-100">
                <CheckIcon class="h-6 w-6 text-green-600" aria-hidden="true" />
              </div>
              <div class="mt-3 text-center sm:mt-5">
                <DialogTitle as="h3" class="text-lg leading-6 font-medium text-gray-900">
                  {{ $t('device_config.update_modal.success_header') }}
                </DialogTitle>
                <div class="mt-2">
                  <p class="text-sm text-gray-500">
                    {{ $t('device_config.update_modal.success_message') }}
                  </p>
                </div>
              </div>
            </div>
            <div v-else>
              <div class="mx-auto flex items-center justify-center h-12 w-12 rounded-full bg-red-100">
                <NoSymbolIcon class="h-6 w-6 text-red-600" aria-hidden="true" />
              </div>
              <div class="mt-3 text-center sm:mt-5">
                <DialogTitle as="h3" class="text-lg leading-6 font-medium text-gray-900">
                  {{ $t('device_config.update_modal.fail_header') }}
                </DialogTitle>
                <div class="mt-2">
                  <p class="text-sm text-gray-500">
                    {{ $t('device_config.update_modal.fail_message') }}
                  </p>
                </div>
              </div>
            </div>

            <div class="mt-5 sm:mt-6">
              <button type="button" class="inline-flex justify-center w-full rounded-md border border-transparent shadow-sm px-4 py-2 bg-indigo-600 text-base font-medium text-white hover:bg-indigo-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500 sm:text-sm" @click="alertOpen = false">
                {{ $t('sitewide.close') }}
              </button>
            </div>
          </div>
        </TransitionChild>
      </div>
    </Dialog>
  </TransitionRoot>
</template>

<script setup>
import { computed, defineProps } from 'vue'
import { CheckIcon, NoSymbolIcon } from "@heroicons/vue/24/outline";
import { Dialog, DialogOverlay, DialogTitle, TransitionChild, TransitionRoot } from "@headlessui/vue";

const props = defineProps({
  'modelValue': {
    required: true,
  },
  'updateSuccessful': {
    type: Boolean,
    required: true,
    default: false,
  }
});

const emit = defineEmits(['update:modelValue'])

const alertOpen = computed({
  get() {
    return props.modelValue
  },
  set(value) {
    emit('update:modelValue', value)
  }
})

</script>


<style scoped>

</style>