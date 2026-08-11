import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";
import { PolynomialRegression } from 'ml-regression-polynomial';

export const useCalibrationStore = defineStore("CalibrationStore", () => {
    
    const calibrationPoints = ref([]);
    const calibrationCoefficients = ref({
        x0: 0,
        x1: 1,
        x2: 0,
        x3: 0
    });
    const calibrationError = ref(false);
    const loaded = ref(false);
    const pending_sync = ref(false);

    /**
     * Mirrors calibrationFilename() in src/http_calibration.cpp:
     *   colour-wide      /conf/<colorIndex>-cal.json
     *   device-specific  /conf/dev-88C255AC2681-cal.json
     */
    function calibrationPointsPath(color, deviceId) {
        if (deviceId) {
            const compact = String(deviceId).replace(/:/g, '').toUpperCase();
            return `/conf/dev-${compact}-cal.json`;
        }
        return `/conf/${color}-cal.json`;
    }

    /**
     * Every mutating endpoint takes an optional deviceId; when it is absent the firmware
     * behaves exactly as it did before and operates on the shared colour configuration.
     */
    function withDeviceId(payload, deviceId) {
        if (deviceId) {
            return { ...payload, deviceId: deviceId };
        }
        return payload;
    }

    async function loadCalibrationPoints(color, deviceId = null) {
        try {
            // no-store because this is re-read immediately after every add and delete; a
            // cached copy shows the previous contents and makes the change look like it
            // never happened. The firmware also sends no-store for /conf/, so this is belt
            // and braces against an older build.
            const response = await fetch(calibrationPointsPath(color, deviceId), { cache: "no-store" });
            if (response.ok) {
                const data = await response.json();
                calibrationPoints.value = data || [];
                loaded.value = true;
                calibrationError.value = false;
            } else {
                calibrationPoints.value = [];
                loaded.value = true;
                calibrationError.value = false;
            }
        } catch (error) {
            calibrationPoints.value = [];
            loaded.value = false;
            calibrationError.value = true;
        }
    }

    async function addCalibrationPoint(color, rawGravity, actualGravity, deviceId = null) {
        try {
            const remote_api = mande("/api/calibration/datapoint/", genCSRFOptions());
            const response = await remote_api.post(withDeviceId({
                color: color,
                rawGravity: rawGravity,
                actualGravity: actualGravity
            }, deviceId));
            if (response) {
                await loadCalibrationPoints(color, deviceId);
                calibrationError.value = false;
                return true;
            } else {
                calibrationError.value = true;
                return false;
            }
        } catch (error) {
            calibrationError.value = true;
            return false;
        }
    }

    async function deleteCalibrationPoint(color, rawGravity, deviceId = null) {
        try {
            const remote_api = mande("/api/calibration/datapoint/delete/", genCSRFOptions());
            const response = await remote_api.post(withDeviceId({
                color: color,
                rawGravity: rawGravity
            }, deviceId));
            if (response) {
                await loadCalibrationPoints(color, deviceId);
                calibrationError.value = false;
                return true;
            } else {
                calibrationError.value = true;
                return false;
            }
        } catch (error) {
            calibrationError.value = true;
            return false;
        }
    }

    function calculateCalibrationCoefficients(points, degree) {
        try {
            if (!points || points.length === 0) {
                // If we have no points, return default coefficients
                // (that is, actual gravity = raw gravity)
                return {
                    x0: 0,
                    x1: 1,
                    x2: 0,
                    x3: 0
                };
            }

            if (degree === 0) {
                // Constant offset - average difference between actual and raw
                const totalDifference = points.reduce((sum, point) => {
                    return sum + (point[1] - point[0]);
                }, 0);
                const averageOffset = totalDifference / points.length;
                
                return {
                    x0: averageOffset,
                    x1: 1,
                    x2: 0,
                    x3: 0
                };
            }

            if (points.length === 1 && degree === 1) {
                const offset = points[0][1] - points[0][0];
                return {
                    x0: offset,
                    x1: 1,
                    x2: 0,
                    x3: 0
                };
            }

            const x = points.map(point => point[0]);
            const y = points.map(point => point[1]);
            
            const regression = new PolynomialRegression(x, y, degree);
            const coefficients = regression.coefficients;
            
            return {
                x0: coefficients[0] || 0,
                x1: coefficients[1] || 1,
                x2: coefficients[2] || 0,
                x3: 0
            };
        } catch (error) {
            return null;
        }
    }

    async function saveCalibrationCoefficients(color, coefficients, deviceId = null) {
        try {
            const remote_api = mande("/api/calibration/coefficients/", genCSRFOptions());
            const response = await remote_api.put(withDeviceId({
                color: color,
                x0: coefficients.x0,
                x1: coefficients.x1,
                x2: coefficients.x2,
                x3: coefficients.x3
            }, deviceId));
            if (response) {
                calibrationCoefficients.value = coefficients;
                calibrationError.value = false;
                return true;
            } else {
                calibrationError.value = true;
                return false;
            }
        } catch (error) {
            calibrationError.value = true;
            return false;
        }
    }

    async function getCalibrationCoefficients(color, deviceId = null) {
        // Per-device coefficients live in the device table, not in the colour settings.
        if (deviceId) {
            try {
                const devices_api = mande("/api/devices/", genCSRFOptions());
                const devices = await devices_api.get();
                const needle = String(deviceId).toLowerCase();
                const record = (devices && devices.devices ? devices.devices : []).find(
                    (d) => (d.deviceId || d.mac || "").toLowerCase() === needle
                );
                if (record && record.hasCalibration && record.cal) {
                    calibrationCoefficients.value = {
                        x0: record.cal.x0 ?? 0,
                        x1: record.cal.x1 ?? 1,
                        x2: record.cal.x2 ?? 0,
                        x3: record.cal.x3 ?? 0
                    };
                    return calibrationCoefficients.value;
                }
                // No device-specific calibration yet - fall through to the colour defaults,
                // which is exactly what the firmware resolves to as well.
            } catch (error) {
                // Fall through to the colour lookup below.
            }
        }

        try {
            const remote_api = mande("/api/settings/json/", genCSRFOptions());
            const response = await remote_api.get();
            if (response) {
                const colorNames = ['Red', 'Green', 'Black', 'Purple', 'Orange', 'Blue', 'Yellow', 'Pink'];
                const colorName = colorNames[color];
                
                if (response[colorName]) {
                    const colorData = response[colorName];
                    calibrationCoefficients.value = {
                        x0: colorData.x0 || 0,
                        x1: colorData.x1 || 1,
                        x2: colorData.x2 || 0,
                        x3: colorData.x3 || 0
                    };
                    return calibrationCoefficients.value;
                }
            }
            // Return default coefficients if not found
            calibrationCoefficients.value = {
                x0: 0,
                x1: 1,
                x2: 0,
                x3: 0
            };
            return calibrationCoefficients.value;
        } catch (error) {
            // Return default coefficients on error
            calibrationCoefficients.value = {
                x0: 0,
                x1: 1,
                x2: 0,
                x3: 0
            };
            return calibrationCoefficients.value;
        }
    }

    function clearStore() {
        calibrationPoints.value = [];
        calibrationCoefficients.value = {
            x0: 0,
            x1: 1,
            x2: 0,
            x3: 0
        };
        calibrationError.value = false;
        loaded.value = false;
    }

    async function clearCalibrationCoefficients(color) {
        const defaultCoefficients = {
            x0: 0,
            x1: 1,
            x2: 0,
            x3: 0
        };
        return await saveCalibrationCoefficients(color, defaultCoefficients);
    }

    function imputeDegreeFromCoefficients(coefficients) {
        if (coefficients.x3 !== 0) return 3;
        if (coefficients.x2 !== 0) return 2;
        if (coefficients.x1 !== 1) return 1;
        return 0;
    }

    async function syncCalToFT(color) {
        try {
            const remote_api = mande("/api/actions/syncCalToFT/", genCSRFOptions());
            const response = await remote_api.post({
                color: color.toLowerCase()
            });
            return response && response.ok;
        } catch (error) {
            return false;
        }
    }

    async function syncCalFromFT(color) {
        try {
            pending_sync.value = true;
            const remote_api = mande("/api/actions/syncCalFromFT/", genCSRFOptions());
            const response = await remote_api.post({
                color: color.toLowerCase()
            });
            return response && response.ok;
        } catch (error) {
            pending_sync.value = false;
            return false;
        }
    }

    async function deleteCal(color) {
        try {
            const remote_api = mande("/api/actions/deleteCal/", genCSRFOptions());
            const response = await remote_api.post({
                color: color.toLowerCase()
            });
            if (response && response.ok) {
                calibrationPoints.value = [];
                calibrationCoefficients.value = {
                    x0: 0,
                    x1: 1,
                    x2: 0,
                    x3: 0
                };
                return true;
            }
            return false;
        } catch (error) {
            return false;
        }
    }

    async function getSyncStatus() {
        try {
            const remote_api = mande("/api/sync/status/");
            const response = await remote_api.get();
            if (response && typeof response.pending_sync === 'boolean') {
                pending_sync.value = response.pending_sync;
                return response.pending_sync;
            }
            return false;
        } catch (error) {
            return false;
        }
    }

    return {
        calibrationPoints,
        calibrationCoefficients,
        calibrationError,
        loaded,
        pending_sync,
        
        loadCalibrationPoints,
        addCalibrationPoint,
        deleteCalibrationPoint,
        calculateCalibrationCoefficients,
        saveCalibrationCoefficients,
        getCalibrationCoefficients,
        clearStore,
        clearCalibrationCoefficients,
        imputeDegreeFromCoefficients,
        syncCalToFT,
        syncCalFromFT,
        deleteCal,
        getSyncStatus
    };
});