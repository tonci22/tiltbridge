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

            <div class="sm:flex sm:items-start">
              <div class="mx-auto flex-shrink-0 flex items-center justify-center h-12 w-12 rounded-full bg-red-100 sm:mx-0 sm:h-10 sm:w-10">
                <ExclamationTriangleIcon class="h-6 w-6 text-red-600" aria-hidden="true" />
              </div>
              <div class="mt-3 text-center sm:mt-0 sm:ml-4 sm:text-left">
                <DialogTitle as="h3" class="text-lg leading-6 font-medium text-gray-900">
                  {{ $t('queue.clear_modal.title') }}
                </DialogTitle>
                <div class="mt-2">
                  <p class="text-sm text-gray-500">
                    {{ $t('queue.clear_modal.message', { count: queuedReadings }) }}
                  </p>
                  <p class="mt-2 text-sm text-gray-500">
                    {{ $t('queue.clear_modal.irreversible') }}
                  </p>
                </div>
              </div>
            </div>

            <!-- The destructive button stays disabled until this is ticked. -->
            <div class="mt-5 rounded-md bg-red-50 px-4 py-3">
              <div class="relative flex items-start">
                <div class="flex items-center h-5">
                  <input id="clear-queue-ack" v-model="acknowledged" type="checkbox" class="focus:ring-red-500 h-4 w-4 text-red-600 border-gray-300 rounded" />
                </div>
                <div class="ml-3 text-sm">
                  <label for="clear-queue-ack" class="font-medium text-red-800">
                    {{ $t('queue.clear_modal.acknowledge', { count: queuedReadings }) }}
                  </label>
                </div>
              </div>
            </div>

            <div class="mt-5 sm:mt-6 sm:flex sm:flex-row-reverse gap-3">
              <button
                  type="button"
                  :disabled="!acknowledged"
                  class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 bg-red-600 text-base font-medium text-white hover:bg-red-700 disabled:bg-gray-400 disabled:cursor-not-allowed focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-red-500 sm:w-auto sm:text-sm"
                  @click="confirm">
                {{ $t('queue.clear_modal.confirm_button') }}
              </button>
              <button
                  type="button"
                  class="mt-3 sm:mt-0 w-full inline-flex justify-center rounded-md border border-gray-300 shadow-sm px-4 py-2 bg-white text-base font-medium text-gray-700 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500 sm:w-auto sm:text-sm"
                  @click="closeModal">
                {{ $t('sitewide.cancel') }}
              </button>
            </div>

          </div>
        </TransitionChild>
      </div>
    </Dialog>
  </TransitionRoot>
</template>

<script setup>
import { computed, ref, watch } from 'vue';
import { ExclamationTriangleIcon } from "@heroicons/vue/24/outline";
import { Dialog, DialogOverlay, DialogTitle, TransitionChild, TransitionRoot } from "@headlessui/vue";

const props = defineProps({
  'modelValue': {
    type: Boolean,
    required: true,
  },
  'queuedReadings': {
    type: Number,
    required: true,
    default: 0,
  },
});

const emit = defineEmits(['update:modelValue', 'confirmed']);

const acknowledged = ref(false);

const modalOpen = computed({
  get() {
    return props.modelValue;
  },
  set(value) {
    emit('update:modelValue', value);
  }
});

// Never carry a previous acknowledgement into a new open.
watch(() => props.modelValue, (isOpen) => {
  if (isOpen) {
    acknowledged.value = false;
  }
});

function closeModal() {
  modalOpen.value = false;
}

function confirm() {
  if (!acknowledged.value) return;
  modalOpen.value = false;
  emit('confirmed');
}
</script>

<style scoped>

</style>
