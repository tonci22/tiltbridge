<template>
  <div v-if="isVisible" class="fixed inset-0 bg-gray-600 bg-opacity-50 overflow-y-auto h-full w-full z-50" @click="handleBackdropClick">
    <div class="relative top-20 mx-auto p-5 border w-11/12 md:w-3/4 lg:w-1/2 xl:w-2/5 shadow-lg rounded-md bg-white">
      <div class="mt-3">
        <!-- Header -->
        <div class="flex items-center justify-between pb-3">
          <h3 class="text-lg font-medium text-gray-900">
            {{ $t('calibration.manual_coefficient_modal.title') }}
          </h3>
          <button @click="closeModal" class="text-gray-400 hover:text-gray-600">
            <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"></path>
            </svg>
          </button>
        </div>

        <!-- Description -->
        <p class="text-sm text-gray-600 mb-4">
          {{ $t('calibration.manual_coefficient_modal.description') }}
        </p>

        <!-- Coefficient Input Fields -->
        <div class="space-y-4 mb-6">
          <div class="grid grid-cols-3 gap-4">
            <div>
              <label for="x0" class="block text-sm font-medium text-gray-700 mb-1">
                {{ $t('calibration.manual_coefficient_modal.x0_label') }}
              </label>
              <input
                id="x0"
                v-model.number="localCoeffs.x0"
                type="number"
                step="any"
                class="w-full px-3 py-2 border border-gray-300 rounded-md shadow-sm focus:outline-none focus:ring-blue-500 focus:border-blue-500"
              />
            </div>
            <div>
              <label for="x1" class="block text-sm font-medium text-gray-700 mb-1">
                {{ $t('calibration.manual_coefficient_modal.x1_label') }}
              </label>
              <input
                id="x1"
                v-model.number="localCoeffs.x1"
                type="number"
                step="any"
                class="w-full px-3 py-2 border border-gray-300 rounded-md shadow-sm focus:outline-none focus:ring-blue-500 focus:border-blue-500"
              />
            </div>
            <div>
              <label for="x2" class="block text-sm font-medium text-gray-700 mb-1">
                {{ $t('calibration.manual_coefficient_modal.x2_label') }}
              </label>
              <input
                id="x2"
                v-model.number="localCoeffs.x2"
                type="number"
                step="any"
                class="w-full px-3 py-2 border border-gray-300 rounded-md shadow-sm focus:outline-none focus:ring-blue-500 focus:border-blue-500"
              />
            </div>
          </div>
        </div>

        <!-- Preview Section -->
        <div class="bg-gray-50 p-4 rounded-lg mb-6">
          <h4 class="text-sm font-medium text-gray-900 mb-3">
            {{ $t('calibration.manual_coefficient_modal.preview_title') }}
          </h4>
          
          <div class="space-y-2 text-sm">
            <div>
              <span class="font-medium text-gray-700">{{ $t('calibration.manual_coefficient_modal.raw_gravity') }}:</span>
              <span class="ml-2 text-gray-900">{{ staticRawGravity }}</span>
            </div>
            
            <div>
              <span class="font-medium text-gray-700">{{ $t('calibration.manual_coefficient_modal.formula') }}:</span>
              <span class="ml-2 text-gray-900 font-mono">{{ formulaText }}</span>
            </div>
            
            <div>
              <span class="font-medium text-gray-700">{{ $t('calibration.manual_coefficient_modal.calibrated_gravity') }}:</span>
              <span class="ml-2 text-gray-900">{{ calibratedGravity }}</span>
            </div>
          </div>
        </div>

        <!-- Action Buttons -->
        <div class="flex justify-end space-x-3">
          <button
            @click="closeModal"
            class="px-4 py-2 text-sm font-medium text-gray-700 bg-gray-100 border border-gray-300 rounded-md hover:bg-gray-200 focus:outline-none focus:ring-2 focus:ring-gray-500"
          >
            {{ $t('sitewide.cancel') }}
          </button>
          <button
            @click="saveCoefficients"
            class="px-4 py-2 text-sm font-medium text-white bg-blue-600 border border-transparent rounded-md hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
          >
            {{ $t('sitewide.save') }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';

const props = defineProps({
  isVisible: {
    type: Boolean,
    default: false
  },
  initialCoefficients: {
    type: Object,
    default: () => ({ x0: 0, x1: 1, x2: 0 })
  },
  currentRawGravity: {
    type: [String, Number],
    default: '--'
  }
});

const emit = defineEmits(['close', 'save']);

const { t: $t } = useI18n();

const localCoeffs = ref({ x0: 0, x1: 1, x2: 0 });
const staticRawGravity = ref('--');

// Watch for modal visibility to capture initial values when opened
watch(() => props.isVisible, (isVisible) => {
  if (isVisible) {
    // Copy the initial coefficients and raw gravity when modal opens
    localCoeffs.value = { ...props.initialCoefficients };
    staticRawGravity.value = props.currentRawGravity;
  }
});

const formulaText = computed(() => {
  const { x0, x1, x2 } = localCoeffs.value;
  const x0Val = typeof x0 === 'number' ? x0 : 0;
  const x1Val = typeof x1 === 'number' ? x1 : 1;
  const x2Val = typeof x2 === 'number' ? x2 : 0;
  return `${x0Val.toFixed(4)} + ${x1Val.toFixed(4)}x + ${x2Val.toFixed(6)}x²`;
});

const calibratedGravity = computed(() => {
  if (staticRawGravity.value === '--' || staticRawGravity.value === '') return '--';
  
  const x = parseFloat(staticRawGravity.value);
  const { x0, x1, x2 } = localCoeffs.value;
  const x0Val = typeof x0 === 'number' ? x0 : 0;
  const x1Val = typeof x1 === 'number' ? x1 : 1;
  const x2Val = typeof x2 === 'number' ? x2 : 0;
  const result = x0Val + x1Val * x + x2Val * x * x;
  return result.toFixed(3);
});

function closeModal() {
  emit('close');
}

function saveCoefficients() {
  emit('save', { ...localCoeffs.value });
}

function handleBackdropClick(event) {
  if (event.target === event.currentTarget) {
    closeModal();
  }
}
</script>