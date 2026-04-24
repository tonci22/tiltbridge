<template>
  <div class="max-w-7xl mx-auto py-6 sm:px-6 lg:px-8">
    <div class="flex-initial md:container">
      <div class="bg-white overflow-hidden sm:rounded-lg sm:shadow">

        <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t("about.reset_actions.header") }}
          </h3>
          <p class="mt-1 max-w-2xl text-sm text-gray-500">
            {{ $t("about.reset_actions.description") }}
          </p>
        </div>

        <div class="px-4 py-5 sm:p-6">
          <div class="grid grid-cols-1 gap-4 sm:grid-cols-2">
            
            <!-- Reset WiFi Button -->
            <div class="bg-gray-50 rounded-lg p-4">
              <h4 class="text-sm font-medium text-gray-900 mb-2">
                {{ $t("about.reset_actions.reset_wifi.title") }}
              </h4>
              <p class="text-sm text-gray-600 mb-4">
                {{ $t("about.reset_actions.reset_wifi.description") }}
              </p>
              <button
                @click="confirmResetWifi"
                class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-orange-600 hover:bg-orange-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-orange-500"
              >
                {{ $t("about.reset_actions.reset_wifi.button") }}
              </button>
            </div>

            <!-- Reset Device Button -->
            <div class="bg-gray-50 rounded-lg p-4">
              <h4 class="text-sm font-medium text-gray-900 mb-2">
                {{ $t("about.reset_actions.reset_device.title") }}
              </h4>
              <p class="text-sm text-gray-600 mb-4">
                {{ $t("about.reset_actions.reset_device.description") }}
              </p>
              <button
                @click="confirmResetDevice"
                class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md text-white bg-red-600 hover:bg-red-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-red-500"
              >
                {{ $t("about.reset_actions.reset_device.button") }}
              </button>
            </div>

          </div>
        </div>

      </div>
    </div>

    <!-- Confirmation Modal -->
    <div v-if="showConfirmModal" class="fixed inset-0 z-50 overflow-y-auto">
      <div class="flex items-end justify-center min-h-screen pt-4 px-4 pb-20 text-center sm:block sm:p-0">
        <div class="fixed inset-0 transition-opacity" aria-hidden="true">
          <div class="absolute inset-0 bg-gray-500 opacity-75"></div>
        </div>

        <span class="hidden sm:inline-block sm:align-middle sm:h-screen" aria-hidden="true">&#8203;</span>

        <div class="inline-block align-bottom bg-white rounded-lg text-left overflow-hidden shadow-xl transform transition-all sm:my-8 sm:align-middle sm:max-w-lg sm:w-full">
          <div class="bg-white px-4 pt-5 pb-4 sm:p-6 sm:pb-4">
            <div class="sm:flex sm:items-start">
              <div class="mx-auto flex-shrink-0 flex items-center justify-center h-12 w-12 rounded-full bg-yellow-100 sm:mx-0 sm:h-10 sm:w-10">
                <svg class="h-6 w-6 text-yellow-600" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-2.5L13.732 4c-.77-.833-1.728-.833-2.498 0L4.316 16.5c-.77.833.192 2.5 1.732 2.5z" />
                </svg>
              </div>
              <div class="mt-3 text-center sm:mt-0 sm:ml-4 sm:text-left">
                <h3 class="text-lg leading-6 font-medium text-gray-900">
                  {{ confirmAction === 'wifi' ? $t("about.reset_actions.confirm_modal.wifi.title") : $t("about.reset_actions.confirm_modal.device.title") }}
                </h3>
                <div class="mt-2">
                  <p class="text-sm text-gray-500">
                    {{ confirmAction === 'wifi' ? $t("about.reset_actions.confirm_modal.wifi.message") : $t("about.reset_actions.confirm_modal.device.message") }}
                  </p>
                </div>
              </div>
            </div>
          </div>
          <div class="bg-gray-50 px-4 py-3 sm:px-6 sm:flex sm:flex-row-reverse">
            <button
              @click="executeReset"
              type="button"
              class="w-full inline-flex justify-center rounded-md border border-transparent shadow-sm px-4 py-2 text-base font-medium text-white focus:outline-none focus:ring-2 focus:ring-offset-2 sm:ml-3 sm:w-auto sm:text-sm"
              :class="confirmAction === 'wifi' ? 'bg-orange-600 hover:bg-orange-700 focus:ring-orange-500' : 'bg-red-600 hover:bg-red-700 focus:ring-red-500'"
            >
              {{ $t("about.reset_actions.confirm_modal.confirm_button") }}
            </button>
            <button
              @click="cancelReset"
              type="button"
              class="mt-3 w-full inline-flex justify-center rounded-md border border-gray-300 shadow-sm px-4 py-2 bg-white text-base font-medium text-gray-700 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500 sm:mt-0 sm:ml-3 sm:w-auto sm:text-sm"
            >
              {{ $t("sitewide.cancel") }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Reset In Progress Modal -->
    <div v-if="showResetModal" class="fixed inset-0 z-50 overflow-y-auto">
      <div class="flex items-end justify-center min-h-screen pt-4 px-4 pb-20 text-center sm:block sm:p-0">
        <div class="fixed inset-0 transition-opacity" aria-hidden="true">
          <div class="absolute inset-0 bg-gray-500 opacity-75"></div>
        </div>

        <span class="hidden sm:inline-block sm:align-middle sm:h-screen" aria-hidden="true">&#8203;</span>

        <div class="inline-block align-bottom bg-white rounded-lg text-left overflow-hidden shadow-xl transform transition-all sm:my-8 sm:align-middle sm:max-w-lg sm:w-full">
          <div class="bg-white px-4 pt-5 pb-4 sm:p-6 sm:pb-4">
            <div class="sm:flex sm:items-start">
              <!-- Spinner (in progress) -->
              <div v-if="!resetSuccess && !resetFailed" class="mx-auto flex-shrink-0 flex items-center justify-center h-12 w-12 rounded-full bg-blue-100 sm:mx-0 sm:h-10 sm:w-10">
                <svg class="h-6 w-6 text-blue-600 animate-spin" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
                  <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                  <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
                </svg>
              </div>
              <!-- Success icon -->
              <div v-else-if="resetSuccess" class="mx-auto flex-shrink-0 flex items-center justify-center h-12 w-12 rounded-full bg-green-100 sm:mx-0 sm:h-10 sm:w-10">
                <svg class="h-6 w-6 text-green-600" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
                </svg>
              </div>
              <!-- Failure icon -->
              <div v-else class="mx-auto flex-shrink-0 flex items-center justify-center h-12 w-12 rounded-full bg-red-100 sm:mx-0 sm:h-10 sm:w-10">
                <svg class="h-6 w-6 text-red-600" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </div>
              <div class="mt-3 text-center sm:mt-0 sm:ml-4 sm:text-left">
                <h3 class="text-lg leading-6 font-medium text-gray-900">
                  <template v-if="resetSuccess">
                    {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.success_title") : $t("about.reset_actions.reset_modal.device.success_title") }}
                  </template>
                  <template v-else-if="resetFailed">
                    {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.failed_title") : $t("about.reset_actions.reset_modal.device.failed_title") }}
                  </template>
                  <template v-else>
                    {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.title") : $t("about.reset_actions.reset_modal.device.title") }}
                  </template>
                </h3>
                <div class="mt-2">
                  <p class="text-sm text-gray-500">
                    <template v-if="resetSuccess">
                      {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.success_message") : $t("about.reset_actions.reset_modal.device.success_message") }}
                    </template>
                    <template v-else-if="resetFailed">
                      {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.failed_message") : $t("about.reset_actions.reset_modal.device.failed_message") }}
                    </template>
                    <template v-else>
                      {{ resetAction === 'wifi' ? $t("about.reset_actions.reset_modal.wifi.message") : $t("about.reset_actions.reset_modal.device.message") }}
                    </template>
                  </p>
                </div>
              </div>
            </div>
          </div>
          <div class="bg-gray-50 px-4 py-3 sm:px-6 sm:flex sm:flex-row-reverse">
            <button
              @click="closeResetModal"
              type="button"
              class="w-full inline-flex justify-center rounded-md border border-gray-300 shadow-sm px-4 py-2 bg-white text-base font-medium text-gray-700 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500 sm:w-auto sm:text-sm"
            >
              {{ $t("sitewide.close") }}
            </button>
          </div>
        </div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref } from 'vue';
import { useConfigStore } from "@/stores/ConfigStore";

const ConfigStore = useConfigStore();

const showConfirmModal = ref(false);
const showResetModal = ref(false);
const confirmAction = ref(null);
const resetAction = ref(null);
const resetSuccess = ref(false);
const resetFailed = ref(false);

const confirmResetWifi = () => {
  confirmAction.value = 'wifi';
  showConfirmModal.value = true;
};

const confirmResetDevice = () => {
  confirmAction.value = 'device';
  showConfirmModal.value = true;
};

const cancelReset = () => {
  showConfirmModal.value = false;
  confirmAction.value = null;
};

const executeReset = async () => {
  showConfirmModal.value = false;
  resetAction.value = confirmAction.value;
  showResetModal.value = true;

  try {
    let success = false;
    if (confirmAction.value === 'wifi') {
      success = await ConfigStore.resetWifi();
    } else if (confirmAction.value === 'device') {
      success = await ConfigStore.resetDevice();
    }

    if (success) {
      resetSuccess.value = true;
    } else {
      resetFailed.value = true;
      console.error('Reset operation failed');
    }
  } catch (error) {
    resetFailed.value = true;
    console.error('Error during reset:', error);
  }

  confirmAction.value = null;
};

const closeResetModal = () => {
  showResetModal.value = false;
  resetAction.value = null;
  resetSuccess.value = false;
  resetFailed.value = false;
};
</script>

<style scoped>

</style>