#ifndef TILTBRIDGE_CALIBRATION_H
#define TILTBRIDGE_CALIBRATION_H

#include <ArduinoJson.h>

// Function prototypes
//
// The optional deviceId selects a per-physical-device calibration set. Passing nullptr
// (the default) keeps the original colour-wide behaviour, so existing callers and any
// install without device-specific configuration are unaffected.
bool processCalibrationDataPoint(const JsonDocument& json, bool triggerUpstreamUpdate);
bool processCalibrationCoefficients(const JsonDocument& json, bool triggerUpstreamUpdate);
bool processCalibrationDataDelete(const JsonDocument& json, bool triggerUpstreamUpdate);
bool getCalibrationPoints(uint8_t color, JsonDocument& doc, const char* deviceId = nullptr);
bool clearCalibrationPoints(uint8_t color, const char* deviceId = nullptr);
bool deleteCalibrationPoint(uint8_t color, double rawGravity, const char* deviceId = nullptr);


/**
 * @brief Delete every calibration point file, per-device and per-colour.
 *
 * Part of a factory reset. The derived coefficients live in tiltbridgeConfig.json and
 * devices.json, but the raw points are separate files that nothing else removes - so a
 * "reset" that promises calibration data is erased was leaving all of it behind.
 */
void deleteAllCalibrationFiles();

#endif // TILTBRIDGE_CALIBRATION_H