<template>
  <div>
    <header class="bg-white shadow">
      <div class="max-w-7xl mx-auto py-6 px-4 sm:px-6 lg:px-8">
        <h1 class="text-3xl font-bold text-gray-900">
          {{ $t('calibration.header', { color: colorName }) }}
        </h1>
      </div>
    </header>
    <main>
      
      <!-- Charts and Current Values Section -->
      <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6 2xl:border-b-0">
        <div class="xl:flex xl:gap-8">
          <!-- Charts Section -->
          <div class="xl:flex-1">
            <h3 class="text-lg leading-6 font-medium text-gray-900 mb-4">
              {{ $t('calibration.chart_section_title') }}
            </h3>
            <div class="flex flex-col">
              <div class="w-full md:min-w-96 max-w-2xl aspect-square">
                <Line v-if="combinedChartData" :data="combinedChartData" :options="chartOptions" />
                <div v-else class="w-full h-full bg-gray-100 border-2 border-dashed border-gray-300 rounded-lg flex flex-col items-center justify-center text-gray-500">
                  <svg class="w-16 h-16 mb-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z"></path>
                  </svg>
                  <p class="text-center text-sm">{{ $t('calibration.chart_placeholder_message') }}</p>
                </div>
              </div>
            </div>
          </div>

          <!-- Current Values and Calibration Points Section -->
          <div class="xl:flex-grow xl:ml-0 mt-8 xl:mt-0">
            <!-- Current Values Section -->
        <h3 class="text-lg leading-6 font-medium text-gray-900 mb-4">
          {{ $t('calibration.current_values_title') }}
        </h3>
        <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
          <div class="bg-gray-50 px-4 py-3 rounded-lg">
            <div class="text-sm font-medium text-gray-500">{{ $t('calibration.raw_tilt_gravity') }}</div>
            <div class="text-lg font-bold text-gray-900">{{ currentRawGravity }}</div>
          </div>
          <div class="bg-gray-50 px-4 py-3 rounded-lg hidden md:block">
            <div class="text-sm font-medium text-gray-500">{{ $t('calibration.current_calibration_function') }}</div>
            <div class="text-lg font-bold text-gray-900">{{ origCalibrationFunction }}</div>
          </div>
          <div class="bg-gray-50 px-4 py-3 rounded-lg hidden md:block">
            <div class="text-sm font-medium text-gray-500">{{ $t('calibration.current_calibrated_gravity') }}</div>
            <div class="text-lg font-bold text-gray-900">{{ origCalibratedGravity }}</div>
          </div>
        </div>

        <div class="grid grid-cols-1 md:grid-cols-3 gap-4 pt-4">
          <div class="bg-gray-50 px-4 py-3 rounded-lg">
            <div class="text-sm font-medium text-gray-500">
              <label for="polynomial-degree" class="block text-sm font-medium text-gray-500">
                {{ $t('calibration.polynomial_degree') }}
              </label>
            </div>
            <div class="text-lg font-bold text-gray-900">
              <div>
                <select id="polynomial-degree" v-model="selectedDegree" class="text-xs mt-0.5 block rounded-md border-gray-300 shadow-sm focus:border-blue-500 focus:ring-blue-500" :disabled="calibrationStore.calibrationPoints.length === 0">
                  <option v-for="degree in availableDegrees" :key="degree" :value="degree">
                    {{ getDegreeDisplayText(degree) }}
                  </option>
                </select>
              </div>
            </div>
          </div>
          <div class="bg-gray-50 px-4 py-3 rounded-lg">
            <div class="text-sm font-medium text-gray-500">{{ $t('calibration.new_calibration_function') }}</div>
            <div class="flex items-center justify-between flex-wrap">
              <button @click="openManualCoefficientModal" class="text-lg font-bold text-gray-900 flex-1 pr-4 text-left hover:text-blue-600 cursor-pointer">{{ calibrationFunction }}</button>
              <button @click="saveCalibration" :disabled="!canSaveOrClear" class="bg-green-600 hover:bg-green-700 disabled:bg-gray-400 text-white font-bold py-2 px-4 rounded text-xs whitespace-nowrap">
                {{ saveButtonLabel }}
              </button>
            </div>
          </div>
          <div class="bg-gray-50 px-4 py-3 rounded-lg">
            <div class="text-sm font-medium text-gray-500">{{ $t('calibration.new_calibrated_gravity') }}</div>
            <div class="text-lg font-bold text-gray-900">{{ calibratedGravity }}</div>
          </div>
        </div>

            <!-- Calibration Points Table (on 2xl+ screens) -->
            <div class="2xl:block hidden mt-8">
              <div class="flex justify-between items-center mb-4">
                <h3 class="text-lg leading-6 font-medium text-gray-900">
                  {{ $t('calibration.calibration_points_title') }}
                </h3>
                <button class="bg-blue-600 hover:bg-blue-700 text-white font-bold py-2 px-4 rounded text-sm" @click="openAddPointModal">
                  {{ $t('calibration.add_calibration_point') }}
                </button>
              </div>
              <table class="min-w-full divide-y divide-gray-200">
                <thead class="bg-gray-50">
                  <tr>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_raw_gravity') }}</th>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_actual_gravity') }}</th>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_current_calibrated') }}</th>
                    <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_new_calibrated') }}</th>
                    <th scope="col" class="relative px-6 py-3"><span class="sr-only">{{ $t('calibration.table_actions') }}</span></th>
                  </tr>
                </thead>
                <tbody class="bg-white divide-y divide-gray-200">
                  <tr v-for="(point, index) in calibrationStore.calibrationPoints" :key="index">
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ point[0] }}</td>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ point[1] }}</td>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-400">{{ getOrigCalibratedGravity(point[0]) }}</td>
                    <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-400">{{ getNewCalibratedGravity(point[0]) }}</td>
                    <td class="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                      <button 
                        @click="deletePoint(point[0])"
                        class="text-red-600 hover:text-red-900"
                      >
                        {{ $t('calibration.delete') }}
                      </button>
                    </td>
                  </tr>
                  <tr v-if="calibrationStore.calibrationPoints.length === 0">
                    <td colspan="5" class="px-6 py-4 whitespace-nowrap text-sm text-gray-500 text-center">
                      {{ $t('calibration.no_calibration_points') }}
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>

      <!-- Calibration Points Table (original position for smaller screens) -->
      <div class="bg-white px-4 py-5 border-b border-gray-200 sm:px-6 2xl:hidden">
        <div class="flex justify-between items-center mb-4">
          <h3 class="text-lg leading-6 font-medium text-gray-900">
            {{ $t('calibration.calibration_points_title') }}
          </h3>
          <button class="bg-blue-600 hover:bg-blue-700 text-white font-bold py-2 px-4 rounded text-sm" @click="openAddPointModal">
            {{ $t('calibration.add_calibration_point') }}
          </button>
        </div>
        <table class="min-w-full divide-y divide-gray-200">
          <thead class="bg-gray-50">
            <tr>
              <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_raw_gravity') }}</th>
              <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">{{ $t('calibration.table_actual_gravity') }}</th>
              <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden lg:table-cell">{{ $t('calibration.table_current_calibrated') }}</th>
              <th scope="col" class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider hidden md:table-cell">{{ $t('calibration.table_new_calibrated') }}</th>
              <th scope="col" class="relative px-6 py-3"><span class="sr-only">{{ $t('calibration.table_actions') }}</span></th>
            </tr>
          </thead>
          <tbody class="bg-white divide-y divide-gray-200">
            <tr v-for="(point, index) in calibrationStore.calibrationPoints" :key="index">
              <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ point[0] }}</td>
              <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">{{ point[1] }}</td>
              <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-400 hidden lg:table-cell">{{ getOrigCalibratedGravity(point[0]) }}</td>
              <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-400 hidden md:table-cell">{{ getNewCalibratedGravity(point[0]) }}</td>
              <td class="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                <button 
                  @click="deletePoint(point[0])"
                  class="text-red-600 hover:text-red-900"
                >
                  {{ $t('calibration.delete') }}
                </button>
              </td>
            </tr>
            <tr v-if="calibrationStore.calibrationPoints.length === 0">
              <td colspan="5" class="px-6 py-4 whitespace-nowrap text-sm text-gray-500 text-center">
                {{ $t('calibration.no_calibration_points') }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>

    </main>
    
    <!-- Add Calibration Point Modal -->
    <AddCalibrationPointModal
      :is-visible="showAddPointModal"
      :color-number="colorNumber"
      :prepopulated-raw-gravity="currentRawGravity"
      @close="showAddPointModal = false"
      @saved="onPointSaved"
    />
    
    <!-- Manual Coefficient Modal -->
    <ManualCoefficientModal
      :is-visible="showManualCoefficientModal"
      :initial-coefficients="getModalCoefficients()"
      :current-raw-gravity="currentRawGravity"
      @close="showManualCoefficientModal = false"
      @save="saveManualCoefficients"
    />
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue';
import { useRoute } from 'vue-router';
import { useI18n } from 'vue-i18n';
import { useTiltStore } from '@/stores/TiltStore';
import { useCalibrationStore } from '@/stores/CalibrationStore';
import { Line } from 'vue-chartjs';
import AddCalibrationPointModal from './AddCalibrationPointModal.vue';
import ManualCoefficientModal from './ManualCoefficientModal.vue';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend
);

const props = defineProps(['color']);
const route = useRoute();
const { t: $t } = useI18n();
const tiltStore = useTiltStore();
const calibrationStore = useCalibrationStore();

const showAddPointModal = ref(false);
const showManualCoefficientModal = ref(false);
const selectedDegree = ref(1);
let intervalObject = null;

const originalCoeffs = ref({
  x0: 0,
  x1: 1,
  x2: 0,
  x3: 0
});

const colorName = computed(() => {
  return $t(`sitewide.tilt_colors.${props.color.toLowerCase()}`);
});

const colorNumber = computed(() => {
  return tiltStore.getColorNumber(props.color);
});

const maxDegree = computed(() => {
  const points = calibrationStore.calibrationPoints.length;
  return Math.max(0, points - 1);
});

const availableDegrees = computed(() => {
  const degrees = [];
  if (calibrationStore.calibrationPoints.length >= 1) {
    degrees.push(0); // Constant offset - always available when we have at least one point
  }
  for (let i = 1; i <= Math.min(maxDegree.value, 2); i++) {
    degrees.push(i);
  }
  return degrees;
});

const currentTilt = computed(() => {
  return tiltStore.tilts.find(tilt => tilt.color.toLowerCase() === props.color.toLowerCase());
});

const currentRawGravity = computed(() => {
  return currentTilt.value ? currentTilt.value.raw_gravity : '--';
});

const calibrationFunction = computed(() => {
  if (calibrationStore.calibrationPoints.length === 0) {
    return $t('calibration.function_no_points');
  }
  
  const tempCoeffs = calibrationStore.calculateCalibrationCoefficients(
    calibrationStore.calibrationPoints, 
    selectedDegree.value
  );
  
  if (!tempCoeffs) {
    return $t('calibration.function_calculation_error');
  }
  
  if (tempCoeffs.x2 !== 0) {
    return `${tempCoeffs.x0.toFixed(4)} + ${tempCoeffs.x1.toFixed(4)}x + ${tempCoeffs.x2.toFixed(6)}x²`;
  } else if (tempCoeffs.x1 !== 1 || tempCoeffs.x0 !== 0) {
    return `${tempCoeffs.x0.toFixed(4)} + ${tempCoeffs.x1.toFixed(4)}x`;
  }
  return $t('calibration.function_no_calibration');
});

const origCalibrationFunction = computed(() => {
  const coeffs = originalCoeffs.value;
  if (coeffs.x2 !== 0) {
    return `${coeffs.x0.toFixed(4)} + ${coeffs.x1.toFixed(4)}x + ${coeffs.x2.toFixed(6)}x²`;
  } else if (coeffs.x1 !== 1 || coeffs.x0 !== 0) {
    return `${coeffs.x0.toFixed(4)} + ${coeffs.x1.toFixed(4)}x`;
  }
  return $t('calibration.function_no_calibration');
});

const calibratedGravity = computed(() => {
  if (!currentTilt.value) return '--';
  return getNewCalibratedGravity(currentTilt.value.raw_gravity);
});

const origCalibratedGravity = computed(() => {
  if (!currentTilt.value) return '--';
  return getOrigCalibratedGravity(currentTilt.value.raw_gravity);
});

const hasCurrentCalibration = computed(() => {
  const coeffs = originalCoeffs.value;
  return coeffs.x1 !== 1 || coeffs.x0 !== 0 || coeffs.x2 !== 0 || coeffs.x3 !== 0;
});

const canSaveOrClear = computed(() => {
  return calibrationStore.calibrationPoints.length > 0 || hasCurrentCalibration.value;
});

const saveButtonLabel = computed(() => {
  if (calibrationStore.calibrationPoints.length === 0 && hasCurrentCalibration.value) {
    return $t('calibration.clear_equation');
  }
  return $t('calibration.save_equation');
});

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  scales: {
    x: {
      type: 'linear',
      position: 'bottom',
      title: {
        display: true,
        text: $t('calibration.chart_raw_gravity_axis')
      },
      min: 0.990,
      max: 1.130
    },
    y: {
      type: 'linear',
      title: {
        display: true,
        text: $t('calibration.chart_actual_gravity_axis')
      },
      min: 0.990,
      max: 1.130
    }
  },
  plugins: {
    legend: {
      display: true
    }
  }
};

const combinedChartData = computed(() => {
  const datasets = [];
  
  // Add calibration points if they exist
  if (calibrationStore.calibrationPoints.length > 0) {
    datasets.push({
      label: $t('calibration.chart_calibration_points'),
      data: calibrationStore.calibrationPoints.map(point => ({
        x: point[0],
        y: point[1]
      })),
      backgroundColor: 'rgb(59, 130, 246)',
      borderColor: 'rgb(59, 130, 246)',
      showLine: false,
      pointRadius: 6,
      pointHoverRadius: 8
    });
  }
  
  // Add current (original) calibration equation line
  const origCoeffs = originalCoeffs.value;
  if (origCoeffs.x1 !== 1 || origCoeffs.x0 !== 0 || origCoeffs.x2 !== 0 || origCoeffs.x3 !== 0) {
    const points = [];
    for (let x = 1.000; x <= 1.125; x += 0.005) {
      const y = origCoeffs.x0 + origCoeffs.x1 * x + origCoeffs.x2 * x * x + origCoeffs.x3 * x * x * x;
      points.push({ x: parseFloat(x.toFixed(4)), y: parseFloat(y.toFixed(4)) });
    }
    
    datasets.push({
      label: $t('calibration.chart_current_calibration'),
      data: points,
      backgroundColor: 'rgb(239, 68, 68)',
      borderColor: 'rgb(239, 68, 68)',
      showLine: true,
      pointRadius: 0,
      tension: 0.1,
      fill: false
    });
  }
  
  // Add new calibration equation line if we have points
  if (calibrationStore.calibrationPoints.length > 0) {
    const newCoeffs = calibrationStore.calculateCalibrationCoefficients(
      calibrationStore.calibrationPoints, 
      selectedDegree.value
    );
    
    if (newCoeffs) {
      const points = [];
      for (let x = 1.000; x <= 1.125; x += 0.005) {
        const y = newCoeffs.x0 + newCoeffs.x1 * x + newCoeffs.x2 * x * x + newCoeffs.x3 * x * x * x;
        points.push({ x: parseFloat(x.toFixed(4)), y: parseFloat(y.toFixed(4)) });
      }
      
      datasets.push({
        label: $t('calibration.chart_new_calibration'),
        data: points,
        backgroundColor: 'rgb(34, 197, 94)',
        borderColor: 'rgb(34, 197, 94)',
        showLine: true,
        pointRadius: 0,
        tension: 0.1,
        fill: false
      });
    }
  }
  
  return datasets.length > 0 ? { datasets } : null;
});

async function deletePoint(rawGravity) {
  await calibrationStore.deleteCalibrationPoint(colorNumber.value, rawGravity);
  
  // If we no longer have enough points to support the selected degree, lower it to the largest supported degree
  const newMaxDegree = Math.max(0, calibrationStore.calibrationPoints.length - 1);
  if (selectedDegree.value > newMaxDegree && selectedDegree.value !== 0) {
    // If constant offset is available, use it, otherwise use the max polynomial degree
    if (calibrationStore.calibrationPoints.length >= 1) {
      selectedDegree.value = 0; // Default to constant offset
    } else {
      selectedDegree.value = Math.max(0, newMaxDegree);
    }
  }
  
  // Automatically recalculate the calibration coefficients after deleting a point
  if (calibrationStore.calibrationPoints.length > 0) {
    const newCoeffs = calibrationStore.calculateCalibrationCoefficients(
      calibrationStore.calibrationPoints, 
      selectedDegree.value
    );
    if (newCoeffs) {
      calibrationStore.calibrationCoefficients = newCoeffs;
    }
  }
}

function openAddPointModal() {
  showAddPointModal.value = true;
}

function openManualCoefficientModal() {
  showManualCoefficientModal.value = true;
}

function getModalCoefficients() {
  if (calibrationStore.calibrationPoints.length === 0) {
    return { x0: 0, x1: 1, x2: 0 };
  }
  
  const tempCoeffs = calibrationStore.calculateCalibrationCoefficients(
    calibrationStore.calibrationPoints, 
    selectedDegree.value
  );
  
  return tempCoeffs || { x0: 0, x1: 1, x2: 0 };
}

async function saveManualCoefficients(coefficients) {
  // Save the manually entered coefficients to the device
  await calibrationStore.saveCalibrationCoefficients(colorNumber.value, coefficients);
  
  // Clear any existing calibration points since we're using manual coefficients
  calibrationStore.calibrationPoints = [];
  
  // Refresh the coefficients from the device to update the "original" coefficients
  await calibrationStore.getCalibrationCoefficients(colorNumber.value).then((coeffs) => {
    originalCoeffs.value = { ...coeffs };
  });
  
  // Refresh tilts to apply new calibration
  await tiltStore.getTilts();
  
  showManualCoefficientModal.value = false;
}

function getDegreeDisplayText(degree) {
  if (degree === 0) return $t('calibration.degree_constant_offset');
  if (degree === 1) return $t('calibration.degree_1st_polynomial');
  if (degree === 2) return $t('calibration.degree_2nd_polynomial');
  if (degree === 3) return $t('calibration.degree_3rd_polynomial');
  return `${degree}th Degree Polynomial`;
}

function getOrigCalibratedGravity(rawGravity) {
  const x = parseFloat(rawGravity);
  const coeffs = originalCoeffs.value;
  const result = coeffs.x0 + coeffs.x1 * x + coeffs.x2 * x * x + coeffs.x3 * x * x * x;
  return result.toFixed(3);
}

function getNewCalibratedGravity(rawGravity) {
  if (calibrationStore.calibrationPoints.length === 0) return '--';
  
  const tempCoeffs = calibrationStore.calculateCalibrationCoefficients(
    calibrationStore.calibrationPoints, 
    selectedDegree.value
  );
  
  if (!tempCoeffs) return '--';
  
  const x = parseFloat(rawGravity);
  const result = tempCoeffs.x0 + tempCoeffs.x1 * x + tempCoeffs.x2 * x * x + tempCoeffs.x3 * x * x * x;
  return result.toFixed(3);
}

async function onPointSaved() {
  // Modal will automatically reload points through the store
  
  // If the currently selected degree is "Constant Offset" and we have more than 1 point, change it to 1st degree polynomial
  if (selectedDegree.value === 0 && calibrationStore.calibrationPoints.length > 1) {
    selectedDegree.value = 1;
  }
  
  // Auto-recalculate the calibration coefficients after adding a point
  if (calibrationStore.calibrationPoints.length > 0) {
    const newCoeffs = calibrationStore.calculateCalibrationCoefficients(
      calibrationStore.calibrationPoints, 
      selectedDegree.value
    );
    if (newCoeffs) {
      calibrationStore.calibrationCoefficients = newCoeffs;
    }
  }
}

async function saveCalibration() {
  if (calibrationStore.calibrationPoints.length === 0 && hasCurrentCalibration.value) {
    // Clear calibration by setting coefficients to default (no calibration)
    const defaultCoeffs = { x0: 0, x1: 1, x2: 0, x3: 0 };
    await calibrationStore.saveCalibrationCoefficients(colorNumber.value, defaultCoeffs).then(() => {
      calibrationStore.getCalibrationCoefficients(colorNumber.value).then((coeffs) => {
        originalCoeffs.value = { ...coeffs };
      });
      tiltStore.getTilts();
    });
  } else {
    // Save new calibration from points
    const coefficients = calibrationStore.calculateCalibrationCoefficients(
        calibrationStore.calibrationPoints,
        selectedDegree.value
    );
    if (coefficients) {
      await calibrationStore.saveCalibrationCoefficients(colorNumber.value, coefficients).then(() => {
        // Once we've saved the coefficients, update the "original" coefficients to match what is on the device
        // (hopefully, what we just saved)
        calibrationStore.getCalibrationCoefficients(colorNumber.value).then((coeffs) => {
          originalCoeffs.value = { ...coeffs }; // Store original coefficients for comparison
        });
        tiltStore.getTilts(); // Refresh tilts to apply new calibration
      });
    }
  }
}

watch(() => maxDegree.value, (newMaxDegree) => {
  if (selectedDegree.value > newMaxDegree && selectedDegree.value !== 0) {
    selectedDegree.value = Math.max(0, newMaxDegree);
  }
});

watch(() => calibrationStore.calibrationPoints.length, (newLength) => {
  if (newLength >= 1 && availableDegrees.value.length > 0) {
    selectedDegree.value = 0; // Default to constant offset when points are available
  }
});

onMounted(async () => {
  await calibrationStore.loadCalibrationPoints(colorNumber.value);
  await calibrationStore.getCalibrationCoefficients(colorNumber.value).then((coeffs) => {
    originalCoeffs.value = { ...coeffs }; // Store original coefficients for comparison
    
    // Impute the degree from the loaded coefficients and set it if we have enough points
    const imputedDegree = calibrationStore.imputeDegreeFromCoefficients(coeffs);
    const maxAvailableDegree = Math.max(0, calibrationStore.calibrationPoints.length - 1);
    
    // Set the selected degree to the imputed degree if we have enough points to support it
    if (calibrationStore.calibrationPoints.length >= 1) {
      if (imputedDegree === 0 || imputedDegree <= maxAvailableDegree) {
        selectedDegree.value = imputedDegree;
      } else {
        // If imputed degree is too high, default to constant offset
        selectedDegree.value = 0;
      }
    }
  });
  await tiltStore.getTilts();
  
  // Set up periodic refresh for Tilt values
  intervalObject = window.setInterval(() => {
    tiltStore.getTilts();
  }, 5000);  // Refresh every 5 seconds to match TiltList component
});

onBeforeUnmount(() => {
  clearInterval(intervalObject);
});
</script>

<style scoped>
</style>