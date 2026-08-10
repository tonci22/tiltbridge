#ifndef TILTBRIDGE_DEVICE_CONFIG_H
#define TILTBRIDGE_DEVICE_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <ArduinoJson.h>

#include "jsonconfig.h"   // TiltCalData, CONFIG_DIR
#include "tilt/tiltHydrometer.h"

// Stored separately from tiltbridgeConfig.json on purpose: that file serialises through a
// single 8192-byte buffer and fails outright when it overflows. A device table has no
// business risking the main config save.
#define DEVICE_CONFIG_FILE CONFIG_DIR "/devices.json"

// 8 rather than 12: this table is statically allocated (~500 B per entry), and on a classic
// ESP32 that RAM is contended by the mbedTLS handshake buffers. Eight covers every Tilt
// colour, and multiple same-colour units beyond that are an unusual setup.
#define MAX_DEVICE_CONFIGS 8
#define MAX_DEVICE_ALIASES 2
#define DEVICE_ID_LEN 18            // "88:C2:55:AC:26:81" + NUL

/**
 * @brief Configuration attached to one physical Tilt, keyed by its BLE address.
 *
 * Colour is not identity: four Red Tilts are four devices. Any field left empty falls
 * back to the legacy colour-keyed configuration, so an install with no devices.json
 * behaves exactly as it did before.
 */
struct DeviceConfig {
    char        deviceId[DEVICE_ID_LEN] = "";
    uint8_t     colorIndex = TILT_NONE;         // last observed colour, for display
    char        friendlyName[33] = "";
    char        googleSheetsName[26] = "";      // same 25-char cap as GsheetsConfig::name
    char        modelLabel[17] = "";            // manual override for the detected model
    char        notes[65] = "";
    bool        enabled = true;
    bool        hasCalibration = false;         // false -> use the colour's calibration
    TiltCalData cal;
    char        aliases[MAX_DEVICE_ALIASES][DEVICE_ID_LEN] = {};
    char        gsheetsLink[256] = "";          // per-device doclongurl cache

    bool isSet() const { return deviceId[0] != '\0'; }
};

class DeviceConfigStore {
public:
    bool load();        // missing or corrupt file leaves an empty table and returns true
    bool save();

    DeviceConfig*       find(const char *deviceId);
    const DeviceConfig* find(const char *deviceId) const;
    DeviceConfig*       findOrCreate(const char *deviceId, uint8_t colorIndex);
    bool                remove(const char *deviceId);

    // Resolved accessors. Each falls back to the colour-keyed config when no
    // device-specific value exists.
    const char* sheetName(const char *deviceId, uint8_t colorIndex) const;
    const char* displayName(const char *deviceId, uint8_t colorIndex) const;
    TiltCalData calibration(const char *deviceId, uint8_t colorIndex) const;
    bool        isEnabled(const char *deviceId) const;
    const char* modelLabel(const char *deviceId, uint8_t colorIndex, bool tiltPro) const;
    const char* sheetLink(const char *deviceId, uint8_t colorIndex) const;
    void        setSheetLink(const char *deviceId, uint8_t colorIndex, const char *link);

    size_t count() const;
    void   to_json(JsonDocument &doc) const;

    // Upsert one device from an API payload. Returns false (with *err set) on a bad
    // deviceId or a full table.
    bool   upsert_from_json(const JsonDocument &doc, const char **err);

    DeviceConfig devices[MAX_DEVICE_CONFIGS];
};

extern DeviceConfigStore device_config;

/**
 * @brief Normalise a BLE address to uppercase colon-separated form.
 *
 * NimBLEAddress::toString() yields lowercase; the spec, the UI and the queue records all
 * use uppercase. Normalising at every boundary keeps lookups exact.
 */
void canonicalizeDeviceId(const char *in, char *out, size_t outSize);

// True when `in` is a well-formed BLE address (xx:xx:xx:xx:xx:xx, any case).
bool isValidDeviceId(const char *in);

#endif // TILTBRIDGE_DEVICE_CONFIG_H
