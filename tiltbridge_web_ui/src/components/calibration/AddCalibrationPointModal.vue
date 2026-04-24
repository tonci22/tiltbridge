<template>
  <div v-if="isVisible" class="fixed inset-0 bg-gray-600 bg-opacity-50 overflow-y-auto h-full w-full z-50">
    <div class="relative top-20 mx-auto p-5 border w-96 shadow-lg rounded-md bg-white">
      <div class="mt-3">
        <!-- Header -->
        <div class="flex justify-between items-center mb-4">
          <h3 class="text-lg font-medium text-gray-900">Add Calibration Point</h3>
          <button 
            @click="closeModal"
            class="text-gray-400 hover:text-gray-600"
          >
            <XMarkIcon class="h-6 w-6" />
          </button>
        </div>

        <!-- Form -->
        <form @submit.prevent="savePoint">
          <div class="mb-4">
            <label for="rawGravity" class="block text-sm font-medium text-gray-700 mb-2">
              Raw Gravity (Tilt Reading)
            </label>
            <input
              id="rawGravity"
              v-model="rawGravity"
              type="number"
              step="0.0001"
              min="0.900"
              max="1.200"
              required
              class="w-full px-3 py-2 border border-gray-300 rounded-md shadow-sm focus:outline-none focus:ring-blue-500 focus:border-blue-500"
              placeholder="1.0500"
            />
          </div>

          <div class="mb-6">
            <label for="actualGravity" class="block text-sm font-medium text-gray-700 mb-2">
              Actual Gravity (Measured)
            </label>
            <input
              id="actualGravity"
              v-model="actualGravity"
              type="number"
              step="0.0001"
              min="0.900"
              max="1.200"
              required
              class="w-full px-3 py-2 border border-gray-300 rounded-md shadow-sm focus:outline-none focus:ring-blue-500 focus:border-blue-500"
              placeholder="1.0480"
            />
          </div>

          <!-- Error Message -->
          <div v-if="errorMessage" class="mb-4 p-3 bg-red-100 border border-red-400 text-red-700 rounded">
            {{ errorMessage }}
          </div>

          <!-- Buttons -->
          <div class="flex justify-end space-x-3">
            <button
              type="button"
              @click="closeModal"
              class="px-4 py-2 text-sm font-medium text-gray-700 bg-gray-200 border border-gray-300 rounded-md hover:bg-gray-300 focus:outline-none focus:ring-2 focus:ring-gray-500"
            >
              Cancel
            </button>
            <button
              type="submit"
              :disabled="saving"
              class="px-4 py-2 text-sm font-medium text-white bg-blue-600 border border-transparent rounded-md hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-blue-500 disabled:bg-gray-400"
            >
              {{ saving ? 'Saving...' : 'Save Point' }}
            </button>
          </div>
        </form>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue';
import { XMarkIcon } from '@heroicons/vue/24/outline';
import { useCalibrationStore } from '@/stores/CalibrationStore';

const props = defineProps({
  isVisible: {
    type: Boolean,
    required: true
  },
  colorNumber: {
    type: Number,
    required: true
  },
  prepopulatedRawGravity: {
    type: String,
    default: ''
  }
});

const emit = defineEmits(['close', 'saved']);

const calibrationStore = useCalibrationStore();

const rawGravity = ref('');
const actualGravity = ref('');
const errorMessage = ref('');
const saving = ref(false);

// Watch for modal visibility to prepopulate raw gravity
watch(() => props.isVisible, (newValue) => {
  if (newValue) {
    rawGravity.value = props.prepopulatedRawGravity;
    actualGravity.value = '';
    errorMessage.value = '';
    saving.value = false;
  }
});

function closeModal() {
  emit('close');
}

async function savePoint() {
  if (saving.value) return;
  
  errorMessage.value = '';
  
  // Validation
  const rawGrav = parseFloat(rawGravity.value);
  const actualGrav = parseFloat(actualGravity.value);
  
  if (isNaN(rawGrav) || rawGrav < 0.900 || rawGrav > 1.200) {
    errorMessage.value = 'Raw gravity must be between 0.900 and 1.200';
    return;
  }
  
  if (isNaN(actualGrav) || actualGrav < 0.900 || actualGrav > 1.200) {
    errorMessage.value = 'Actual gravity must be between 0.900 and 1.200';
    return;
  }

  // Check if this raw gravity already exists
  const existingPoint = calibrationStore.calibrationPoints.find(
    point => Math.abs(point[0] - rawGrav) < 0.0001
  );
  
  if (existingPoint) {
    errorMessage.value = 'A calibration point with this raw gravity already exists';
    return;
  }

  saving.value = true;
  
  try {
    const success = await calibrationStore.addCalibrationPoint(
      props.colorNumber,
      rawGrav,
      actualGrav
    );
    
    if (success) {
      emit('saved');
      closeModal();
    } else {
      errorMessage.value = 'Failed to save calibration point. Please try again.';
    }
  } catch (error) {
    errorMessage.value = 'An error occurred while saving. Please try again.';
  } finally {
    saving.value = false;
  }
}
</script>

<style scoped>
</style>