#include "http_calibration.h"
#include <ArduinoJson.h>
#include <thorlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>   // fabs - without this abs() resolves to the integer overload
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include "filesystem.h"
#include "jsonconfig.h"
#include "device_config.h"
#include "tilt/tiltHydrometer.h"
#include "JsonKeys.h"

// Buffer size for JSON file operations
#define JSON_FILE_BUFFER_SIZE 2048

/**
 * @brief Build the calibration-points filename for a colour or a specific device.
 *
 * Colour-wide:      /littlefs/conf/0-cal.json          (unchanged, pre-existing files)
 * Device-specific:  /littlefs/conf/dev-88C255AC2681-cal.json
 *
 * Colons are stripped so the name stays short and filesystem-safe.
 */
static void calibrationFilename(const char* deviceId, uint8_t color, char* out, size_t outSize) {
    if (deviceId != nullptr && deviceId[0] != '\0') {
        char compact[13] = {0};
        size_t j = 0;
        for (size_t i = 0; deviceId[i] != '\0' && j < sizeof(compact) - 1; i++) {
            if (deviceId[i] != ':')
                compact[j++] = (char)toupper((unsigned char)deviceId[i]);
        }
        compact[j] = '\0';
        snprintf(out, outSize, "%s/dev-%s-cal.json", CONFIG_DIR, compact);
        return;
    }

    snprintf(out, outSize, "%s/%d-cal.json", CONFIG_DIR, color);
}

/**
 * @brief Pull an optional, validated deviceId out of a request body.
 * @return canonical id written into `out`, or nullptr when absent/invalid.
 */
static const char* optionalDeviceId(const JsonDocument& json, char* out, size_t outSize) {
    const char* raw = json["deviceId"].as<const char*>();
    if (!isValidDeviceId(raw))
        return nullptr;

    canonicalizeDeviceId(raw, out, outSize);
    return out;
}

/**
 * @brief Read file contents into a dynamically allocated buffer
 * @param filename Path to file
 * @param out_size Output parameter for file size
 * @return Allocated buffer (caller must free) or NULL on error
 */
static char* read_file_to_buffer(const char* filename, size_t* out_size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size == 0 || size > JSON_FILE_BUFFER_SIZE) {
        fclose(file);
        return NULL;
    }

    // Allocate buffer
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    // Read file
    size_t read = fread(buffer, 1, size, file);
    fclose(file);

    if (read != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

/**
 * @brief Write JSON document to file via buffer
 * @param filename Path to file
 * @param doc JSON document to write
 * @return true on success
 */
static bool write_json_to_file(const char* filename, JsonDocument& doc) {
    // Measure required size
    size_t json_size = measureJson(doc);
    if (json_size == 0 || json_size > JSON_FILE_BUFFER_SIZE) {
        return false;
    }

    // Allocate buffer
    char* buffer = (char*)malloc(json_size + 1);
    if (!buffer) {
        return false;
    }

    // Serialize to buffer
    serializeJson(doc, buffer, json_size + 1);

    // Write to file
    FILE *file = fopen(filename, "w");
    if (!file) {
        free(buffer);
        return false;
    }

    size_t written = fwrite(buffer, 1, json_size, file);
    fclose(file);
    free(buffer);

    return written == json_size;
}

/**
 * @brief Read JSON document from file via buffer
 * @param filename Path to file
 * @param doc JSON document to populate
 * @return DeserializationError
 */
static DeserializationError read_json_from_file(const char* filename, JsonDocument& doc) {
    size_t size;
    char* buffer = read_file_to_buffer(filename, &size);
    if (!buffer) {
        return DeserializationError::EmptyInput;
    }

    DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    return error;
}

// Function to handle calibration data points via POST
bool processCalibrationDataPoint(const JsonDocument& json, bool triggerUpstreamUpdate) {
    // Extract required fields
    if (!json["color"].is<uint8_t>() || !json["rawGravity"].is<double>() || !json["actualGravity"].is<double>()) {
        Log.error("Error: Missing required calibration data fields.\r\n");
        return false;
    }

    uint8_t color = json["color"].as<uint8_t>();

    // Validate color
    if (color >= TILT_COLORS) {
        Log.error("Error: Invalid Tilt color: %d.\r\n", color);
        return false;
    }

    double rawGravity = json["rawGravity"].as<double>();
    double actualGravity = json["actualGravity"].as<double>();

    // Create filename for calibration data (device-specific when a deviceId was supplied)
    char deviceIdBuf[DEVICE_ID_LEN];
    const char* deviceId = optionalDeviceId(json, deviceIdBuf, sizeof(deviceIdBuf));

    char filename[64];
    calibrationFilename(deviceId, color, filename, sizeof(filename));

    // Load existing calibration data or create new document
    JsonDocument calDoc;
    JsonArray dataPoints;

    if (filesystem_exists(filename)) {
        // Load existing file
        DeserializationError error = read_json_from_file(filename, calDoc);
        if (error) {
            Log.error("Error: Failed to parse existing calibration file: %s\r\n", error.c_str());
            // Create new document if parsing fails
            dataPoints = calDoc.to<JsonArray>();
        } else {
            dataPoints = calDoc.as<JsonArray>();
        }
    } else {
        // Create new document
        dataPoints = calDoc.to<JsonArray>();
    }

    // Add new calibration point as array [rawGravity, actualGravity]
    JsonArray newPoint = dataPoints.add<JsonArray>();
    newPoint.add(rawGravity);
    newPoint.add(actualGravity);

    // Save to file
    if (!write_json_to_file(filename, calDoc)) {
        Log.error("Error: Failed to write calibration file.\r\n");
        return false;
    }

    /*
     * ThorLog has no %f - its format parser does not recognise '.', so "%.3f" consumed
     * no vararg and printed literally as "3f", which also desynchronised every later
     * specifier in the same message. Its %F is fixed at 2 decimals, too coarse for
     * gravity. Pre-format with snprintf and log the strings.
     */
    char rawStr[16], actualStr[16];
    snprintf(rawStr, sizeof(rawStr), "%.3f", rawGravity);
    snprintf(actualStr, sizeof(actualStr), "%.3f", actualGravity);
    Log.notice("Calibration point saved: %s=%s, raw=%s, actual=%s\r\n",
               deviceId ? "device" : "color",
               deviceId ? deviceId : tilt_color_names[color],
               rawStr, actualStr);

    return true;
}

// Function to handle polynomial coefficients via PUT/PATCH
bool processCalibrationCoefficients(const JsonDocument& json, bool triggerUpstreamUpdate) {
    // Extract required fields
    if (!json["color"].is<uint8_t>()) {
        Log.error("Error: Missing color field for calibration coefficients.\r\n");
        return false;
    }

    uint8_t color = json["color"].as<uint8_t>();

    // Validate color
    if (color >= TILT_COLORS) {
        Log.error("Error: Invalid Tilt color: %d.\r\n", color);
        return false;
    }

    char deviceIdBuf[DEVICE_ID_LEN];
    const char* deviceId = optionalDeviceId(json, deviceIdBuf, sizeof(deviceIdBuf));

    // Point the writes at the device's own coefficients when a deviceId was supplied,
    // otherwise at the shared colour coefficients exactly as before.
    TiltCalData* target = nullptr;
    DeviceConfig* dev = nullptr;

    if (deviceId != nullptr) {
        dev = device_config.findOrCreate(deviceId, color);
        if (dev == nullptr) {
            Log.error("Error: Unable to create device configuration for %s (table full?).\r\n", deviceId);
            return false;
        }
        target = &dev->cal;
    } else {
        target = &config.tilt_calibration[color];
    }

    // Update coefficients if provided
    bool updated = false;

    if (json["x0"].is<double>()) {
        target->x0 = json["x0"].as<double>();
        updated = true;
    }

    if (json["x1"].is<double>()) {
        target->x1 = json["x1"].as<double>();
        updated = true;
    }

    if (json["x2"].is<double>()) {
        target->x2 = json["x2"].as<double>();
        updated = true;
    }

    if (json["x3"].is<double>()) {
        target->x3 = json["x3"].as<double>();
        updated = true;
    }

    if (updated) {
        // Pre-formatted for the same reason as above: ThorLog cannot render %f.
        char coeffStr[80];
        snprintf(coeffStr, sizeof(coeffStr), "%.6f, %.6f, %.6f, %.6f",
                 target->x0, target->x1, target->x2, target->x3);

        if (dev != nullptr) {
            // Flipping this is what makes the device stop falling back to its colour.
            dev->hasCalibration = true;
            if (!device_config.save()) {
                Log.error("Error: Unable to save device calibration coefficients.\r\n");
                return false;
            }
            Log.notice("Calibration coefficients saved for device %s (%s)\r\n",
                       deviceId, coeffStr);
        } else {
            if (!config.save()) {
                Log.error("Error: Unable to save calibration coefficients.\r\n");
                return false;
            }
            Log.notice("Calibration coefficients saved for color %d (%s)\r\n",
                       color, coeffStr);
        }
    }

    return true;
}

// Function to get calibration points for a specific color
bool getCalibrationPoints(uint8_t color, JsonDocument& doc, const char* deviceId) {
    if (color >= TILT_COLORS) {
        Log.error("Error: Invalid Tilt color: %d.\r\n", color);
        return false;
    }

    char filename[64];
    calibrationFilename(deviceId, color, filename, sizeof(filename));

    if (!filesystem_exists(filename)) {
        // Return empty array if file doesn't exist
        doc.to<JsonArray>();
        return true;
    }

    DeserializationError error = read_json_from_file(filename, doc);
    if (error) {
        Log.error("Error: Failed to parse calibration file: %s\r\n", error.c_str());
        return false;
    }

    return true;
}

// Function to clear calibration points for a specific color
bool clearCalibrationPoints(uint8_t color, const char* deviceId) {
    if (color >= TILT_COLORS) {
        Log.error("Error: Invalid Tilt color: %d.\r\n", color);
        return false;
    }

    char filename[64];
    calibrationFilename(deviceId, color, filename, sizeof(filename));

    if (filesystem_exists(filename)) {
        if (!(remove(filename) == 0)) {
            Log.error("Error: Failed to delete calibration file.\r\n");
            return false;
        }
        Log.notice("Calibration points cleared for %s\r\n",
                   deviceId ? deviceId : tilt_color_names[color]);
    }

    return true;
}

void deleteAllCalibrationFiles() {
    /*
     * One file per pass: open the directory, take the first calibration file, close, delete.
     *
     * This used to collect every match up front into
     * `char victims[MAX_DEVICE_CONFIGS + TILT_COLORS][sizeof(CONFIG_DIR) + 1 + 256]` - 16
     * rows of 272 bytes, 4,352 of the 8 KB loopTask stack, which is where a factory reset
     * runs. It also stopped silently at 16 entries, so a device with more calibration files
     * than that kept the remainder across a reset that claims to erase everything.
     *
     * Reopening per file preserves the reason the collect-then-delete shape existed -
     * removing entries while walking a directory is not guaranteed safe on LittleFS - at
     * one 272-byte buffer instead of sixteen. A factory reset deletes a handful of files
     * once, so the repeated scans cost nothing anybody can measure.
     */
    size_t count = 0;

    for (;;) {
        DIR *dir = opendir(CONFIG_DIR);
        if (dir == nullptr)
            break;

        // sizeof(CONFIG_DIR) + '/' + the longest name LittleFS will hand back.
        char victim[sizeof(CONFIG_DIR) + 1 + 256];
        bool found = false;

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            const char *name = entry->d_name;
            const size_t len = strlen(name);

            // "dev-<MAC>-cal.json" and "<colour index>-cal.json"
            if (len < 9 || strcmp(name + len - 9, "-cal.json") != 0)
                continue;

            snprintf(victim, sizeof(victim), "%s/%s", CONFIG_DIR, name);
            found = true;
            break;
        }
        closedir(dir);

        if (!found)
            break;

        if (remove(victim) != 0) {
            // The next pass would select the same file and fail the same way, so stop
            // rather than spin on it.
            Log.error("Unable to delete %s during factory reset.\r\n", victim);
            break;
        }
        count++;
    }

    if (count > 0)
        Log.warning("Deleted %u calibration point file%s.\r\n",
                    (unsigned)count, (count == 1) ? "" : "s");
}

// Function to delete individual calibration data point by raw gravity
bool deleteCalibrationPoint(uint8_t color, double rawGravity, const char* deviceId) {
    if (color >= TILT_COLORS) {
        Log.error("Error: Invalid Tilt color: %d.\r\n", color);
        return false;
    }

    char filename[64];
    calibrationFilename(deviceId, color, filename, sizeof(filename));

    if (!filesystem_exists(filename)) {
        Log.error("Error: No calibration file exists for %s.\r\n",
                  deviceId ? deviceId : tilt_color_names[color]);
        return false;
    }

    // Load existing calibration data
    JsonDocument calDoc;
    DeserializationError error = read_json_from_file(filename, calDoc);
    if (error) {
        Log.error("Error: Failed to parse calibration file: %s\r\n", error.c_str());
        return false;
    }

    JsonArray dataPoints = calDoc.as<JsonArray>();

    /*
     * Delete the CLOSEST point, not the first one inside a tolerance.
     *
     * This previously used abs() with only <stdlib.h> in scope, so it resolved to the integer
     * abs(int): the double difference truncated to 0, `0 < 0.001` was true for any two points
     * less than 1.0 apart, and the loop - running backwards and breaking on first match -
     * always deleted the LAST point whatever was asked for. It still answered {"status":"ok"},
     * so the row the user clicked stayed and a different one silently disappeared.
     *
     * fabs() alone would not be enough. Gravity points are four decimals and are routinely
     * closer together than the old 0.001 tolerance - two points at 1.0024 and 1.0026 are
     * 0.0002 apart - so any "first within tolerance" rule stays ambiguous. Picking the nearest
     * is unambiguous whenever the values differ at all, and the tolerance then only guards
     * against deleting something unrelated when the requested value is not present.
     */
    int bestIndex = -1;
    double bestDelta = 0.0;

    for (int i = 0; i < (int)dataPoints.size(); i++) {
        JsonArray point = dataPoints[i];
        if (point.size() < 2 || !point[0].is<double>())
            continue;

        const double delta = fabs(point[0].as<double>() - rawGravity);
        if (bestIndex < 0 || delta < bestDelta) {
            bestIndex = i;
            bestDelta = delta;
        }
    }

    // Half a step of the fourth decimal the UI stores, so an exact value always matches and a
    // value that is simply absent never does.
    const bool found = (bestIndex >= 0 && bestDelta < 0.00005);

    if (found)
        dataPoints.remove(bestIndex);

    // Pre-formatted: ThorLog cannot render %f (see processCalibrationDataPoint).
    char rawStr[16];
    snprintf(rawStr, sizeof(rawStr), "%.3f", rawGravity);

    if (!found) {
        Log.error("Error: Calibration point with raw gravity %s not found.\r\n", rawStr);
        return false;
    }

    // Save updated data back to file
    if (!write_json_to_file(filename, calDoc)) {
        Log.error("Error: Failed to write calibration file.\r\n");
        return false;
    }

    Log.notice("Calibration point deleted: color=%d, raw=%s\r\n", color, rawStr);
    return true;
}

// Function to handle calibration data points via DELETE
bool processCalibrationDataDelete(const JsonDocument& json, bool triggerUpstreamUpdate) {
    // Extract required fields
    if (!json["color"].is<uint8_t>()) {
        Log.error("Error: Missing color field for calibration deletion.\r\n");
        return false;
    }

    uint8_t color = json["color"].as<uint8_t>();

    char deviceIdBuf[DEVICE_ID_LEN];
    const char* deviceId = optionalDeviceId(json, deviceIdBuf, sizeof(deviceIdBuf));

    // Check if rawGravity is provided for individual point deletion
    if (json["rawGravity"].is<double>()) {
        double rawGravity = json["rawGravity"].as<double>();
        return deleteCalibrationPoint(color, rawGravity, deviceId);
    } else {
        // No rawGravity provided, clear all points
        return clearCalibrationPoints(color, deviceId);
    }
}
