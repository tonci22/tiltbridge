#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <thorlog.h>

#include "device_config.h"
#include "filesystem.h"

DeviceConfigStore device_config;

#define DEVICE_CONFIG_BUFFER_SIZE 4096

//=============================================================================
// Device id helpers
//=============================================================================

bool isValidDeviceId(const char *in) {
    if (in == nullptr || strlen(in) != 17)
        return false;

    for (int i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (in[i] != ':')
                return false;
        } else if (!isxdigit((unsigned char)in[i])) {
            return false;
        }
    }
    return true;
}

void canonicalizeDeviceId(const char *in, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return;

    out[0] = '\0';
    if (in == nullptr)
        return;

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < outSize; i++) {
        out[j++] = (char)toupper((unsigned char)in[i]);
    }
    out[j] = '\0';
}

//=============================================================================
// Lookup
//=============================================================================

DeviceConfig* DeviceConfigStore::find(const char *deviceId) {
    if (deviceId == nullptr || deviceId[0] == '\0')
        return nullptr;

    for (auto &d : devices) {
        if (!d.isSet())
            continue;

        if (strcmp(d.deviceId, deviceId) == 0)
            return &d;

        // A repeater rebroadcasts the same physical Tilt under a different MAC.
        for (const auto &alias : d.aliases) {
            if (alias[0] != '\0' && strcmp(alias, deviceId) == 0)
                return &d;
        }
    }
    return nullptr;
}

const DeviceConfig* DeviceConfigStore::find(const char *deviceId) const {
    return const_cast<DeviceConfigStore*>(this)->find(deviceId);
}

DeviceConfig* DeviceConfigStore::findOrCreate(const char *deviceId, uint8_t colorIndex) {
    DeviceConfig *existing = find(deviceId);
    if (existing != nullptr) {
        if (colorIndex < TILT_COLORS)
            existing->colorIndex = colorIndex;
        return existing;
    }

    if (!isValidDeviceId(deviceId))
        return nullptr;

    for (auto &d : devices) {
        if (d.isSet())
            continue;

        d = DeviceConfig{};
        strlcpy(d.deviceId, deviceId, sizeof(d.deviceId));
        d.colorIndex = colorIndex;
        return &d;
    }

    return nullptr;   // table full
}

void DeviceConfigStore::eraseAll() {
    for (uint8_t i = 0; i < MAX_DEVICE_CONFIGS; i++)
        devices[i] = DeviceConfig{};

    ::remove(DEVICE_CONFIG_FILE);
    Log.warning("Device configurations erased.\r\n");
}

bool DeviceConfigStore::remove(const char *deviceId) {
    DeviceConfig *d = find(deviceId);
    if (d == nullptr)
        return false;

    *d = DeviceConfig{};
    return true;
}

size_t DeviceConfigStore::count() const {
    size_t n = 0;
    for (const auto &d : devices) {
        if (d.isSet())
            n++;
    }
    return n;
}

//=============================================================================
// Resolved accessors - device value first, colour config as fallback
//=============================================================================

/*
 * A sheet belongs to a Tilt, so the per-device name always wins.
 *
 * What to do WITHOUT one changed once two Tilts of the same colour became possible. The
 * per-colour name is only unambiguous while exactly one Tilt of that colour exists; handing
 * it to a second one would silently merge two ferments into a single sheet - far worse than
 * any naming mistake, and effectively impossible to unpick afterwards.
 *
 * So a device with NO configuration at all gets a name derived from its own address,
 * "Red-396D": it cannot collide, and it reads as a placeholder asking to be configured. A
 * device that HAS a configuration but left the name blank still falls back to the colour,
 * because that is a deliberate choice made in front of the per-device field.
 */
void DeviceConfigStore::sheetName(const char *deviceId, uint8_t colorIndex,
                                  char *out, size_t outSize) const {
    if (out == nullptr || outSize == 0)
        return;

    out[0] = '\0';

    const DeviceConfig *d = find(deviceId);

    if (d != nullptr) {
        if (d->googleSheetsName[0] != '\0')
            strlcpy(out, d->googleSheetsName, outSize);
        else if (colorIndex < TILT_COLORS)
            strlcpy(out, config.gsheets_config[colorIndex].name, outSize);
        return;
    }

    // Unconfigured: last two octets of the address, which are unique enough in practice and
    // short enough to leave the name readable.
    char suffix[5] = {0};
    if (deviceId != nullptr) {
        const size_t len = strlen(deviceId);
        const char *tail = (len >= 5) ? (deviceId + len - 5) : deviceId;
        size_t s = 0;
        for (size_t i = 0; tail[i] != '\0' && s < sizeof(suffix) - 1; i++) {
            if (tail[i] != ':')
                suffix[s++] = tail[i];
        }
    }

    const char *colorName = (colorIndex < TILT_COLORS) ? tilt_color_names[colorIndex] : "Tilt";

    if (suffix[0] == '\0')
        strlcpy(out, colorName, outSize);
    else
        snprintf(out, outSize, "%s-%s", colorName, suffix);
}

const char* DeviceConfigStore::displayName(const char *deviceId, uint8_t colorIndex) const {
    const DeviceConfig *d = find(deviceId);
    if (d != nullptr && d->friendlyName[0] != '\0')
        return d->friendlyName;

    if (colorIndex < TILT_COLORS)
        return tilt_color_names[colorIndex];

    return "Unknown";
}

TiltCalData DeviceConfigStore::calibration(const char *deviceId, uint8_t colorIndex) const {
    const DeviceConfig *d = find(deviceId);
    if (d != nullptr && d->hasCalibration)
        return d->cal;

    if (colorIndex < TILT_COLORS)
        return config.tilt_calibration[colorIndex];

    return TiltCalData{};
}

bool DeviceConfigStore::isEnabled(const char *deviceId) const {
    const DeviceConfig *d = find(deviceId);
    // No entry means the device has never been configured, which must behave exactly as
    // it did before this feature existed: enabled.
    return (d == nullptr) ? true : d->enabled;
}

const char* DeviceConfigStore::modelLabel(const char *deviceId, uint8_t colorIndex, bool tiltPro) const {
    (void)colorIndex;
    const DeviceConfig *d = find(deviceId);
    if (d != nullptr && d->modelLabel[0] != '\0')
        return d->modelLabel;

    return tiltPro ? "Pro Family" : "Standard";
}

const char* DeviceConfigStore::sheetLink(const char *deviceId, uint8_t colorIndex) const {
    const DeviceConfig *d = find(deviceId);
    if (d != nullptr && d->gsheetsLink[0] != '\0')
        return d->gsheetsLink;

    if (colorIndex < TILT_COLORS)
        return config.gsheets_config[colorIndex].link;

    return "";
}

void DeviceConfigStore::setSheetLink(const char *deviceId, uint8_t colorIndex, const char *link) {
    if (link == nullptr)
        return;

    DeviceConfig *d = find(deviceId);
    if (d != nullptr) {
        if (strcmp(d->gsheetsLink, link) != 0) {
            strlcpy(d->gsheetsLink, link, sizeof(d->gsheetsLink));
            save();
        }
        return;
    }

    if (colorIndex < TILT_COLORS &&
        strcmp(config.gsheets_config[colorIndex].link, link) != 0) {
        strlcpy(config.gsheets_config[colorIndex].link, link,
                sizeof(config.gsheets_config[colorIndex].link));
        config.save();
    }
}

//=============================================================================
// Serialisation
//=============================================================================

void DeviceConfigStore::to_json(JsonDocument &doc) const {
    doc["schemaVersion"] = 1;
    doc["maxDevices"] = MAX_DEVICE_CONFIGS;

    JsonArray arr = doc["devices"].to<JsonArray>();
    for (const auto &d : devices) {
        if (!d.isSet())
            continue;

        JsonObject o = arr.add<JsonObject>();
        o["deviceId"] = d.deviceId;
        o["mac"] = d.deviceId;      // same value; the spec's payload carries both
        o["colorIndex"] = d.colorIndex;
        o["color"] = (d.colorIndex < TILT_COLORS) ? tilt_color_names[d.colorIndex] : "Unknown";
        o["friendlyName"] = d.friendlyName;
        o["googleSheetsName"] = d.googleSheetsName;
        o["modelLabel"] = d.modelLabel;
        o["notes"] = d.notes;
        o["enabled"] = d.enabled;
        o["hasCalibration"] = d.hasCalibration;
        o["gsheetsLink"] = d.gsheetsLink;

        JsonObject cal = o["cal"].to<JsonObject>();
        cal["x0"] = d.cal.x0;
        cal["x1"] = d.cal.x1;
        cal["x2"] = d.cal.x2;
        cal["x3"] = d.cal.x3;

        JsonArray al = o["aliases"].to<JsonArray>();
        for (const auto &alias : d.aliases) {
            if (alias[0] != '\0')
                al.add(alias);
        }
    }
}

bool DeviceConfigStore::upsert_from_json(const JsonDocument &doc, const char **err) {
    const char *rawId = doc["deviceId"].as<const char*>();
    if (rawId == nullptr)
        rawId = doc["mac"].as<const char*>();

    if (!isValidDeviceId(rawId)) {
        if (err) *err = "Invalid or missing deviceId (expected xx:xx:xx:xx:xx:xx)";
        return false;
    }

    char id[DEVICE_ID_LEN];
    canonicalizeDeviceId(rawId, id, sizeof(id));

    uint8_t colorIndex = TILT_NONE;
    if (doc["colorIndex"].is<uint8_t>()) {
        uint8_t c = doc["colorIndex"].as<uint8_t>();
        if (c < TILT_COLORS)
            colorIndex = c;
    }

    DeviceConfig *d = findOrCreate(id, colorIndex);
    if (d == nullptr) {
        if (err) *err = "Device table is full";
        return false;
    }

    if (doc["friendlyName"].is<const char*>())
        strlcpy(d->friendlyName, doc["friendlyName"].as<const char*>(), sizeof(d->friendlyName));

    if (doc["googleSheetsName"].is<const char*>())
        strlcpy(d->googleSheetsName, doc["googleSheetsName"].as<const char*>(), sizeof(d->googleSheetsName));

    if (doc["modelLabel"].is<const char*>())
        strlcpy(d->modelLabel, doc["modelLabel"].as<const char*>(), sizeof(d->modelLabel));

    if (doc["notes"].is<const char*>())
        strlcpy(d->notes, doc["notes"].as<const char*>(), sizeof(d->notes));

    if (doc["enabled"].is<bool>())
        d->enabled = doc["enabled"].as<bool>();

    if (doc["aliases"].is<JsonArrayConst>()) {
        for (auto &alias : d->aliases)
            alias[0] = '\0';

        size_t i = 0;
        for (JsonVariantConst v : doc["aliases"].as<JsonArrayConst>()) {
            if (i >= MAX_DEVICE_ALIASES)
                break;
            const char *a = v.as<const char*>();
            if (isValidDeviceId(a)) {
                canonicalizeDeviceId(a, d->aliases[i], DEVICE_ID_LEN);
                i++;
            }
        }
    }

    if (!save()) {
        if (err) *err = "Unable to save device configuration";
        return false;
    }
    return true;
}

bool DeviceConfigStore::load() {
    for (auto &d : devices)
        d = DeviceConfig{};

    if (!filesystem_exists(DEVICE_CONFIG_FILE)) {
        Log.info("No device config file - all Tilts will use colour-based settings.\r\n");
        return true;
    }

    FILE *f = fopen(DEVICE_CONFIG_FILE, "r");
    if (f == nullptr) {
        Log.error("Unable to open %s.\r\n", DEVICE_CONFIG_FILE);
        return true;   // degrade to colour-only rather than failing boot
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size == 0 || size > DEVICE_CONFIG_BUFFER_SIZE) {
        Log.error("Device config file has implausible size %u; ignoring.\r\n", (unsigned)size);
        fclose(f);
        return true;
    }

    char *buffer = (char*)malloc(size + 1);
    if (buffer == nullptr) {
        fclose(f);
        return true;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);
    buffer[read] = '\0';

    JsonDocument doc;
    DeserializationError parseErr = deserializeJson(doc, buffer);
    free(buffer);

    if (parseErr) {
        // Corrupt file degrades to colour-only behaviour. It is replaced on the next save.
        Log.error("Device config is not valid JSON (%s); falling back to colour settings.\r\n",
                  parseErr.c_str());
        return true;
    }

    size_t i = 0;
    for (JsonObjectConst o : doc["devices"].as<JsonArrayConst>()) {
        if (i >= MAX_DEVICE_CONFIGS)
            break;

        const char *id = o["deviceId"].as<const char*>();
        if (!isValidDeviceId(id))
            continue;

        DeviceConfig &d = devices[i];
        d = DeviceConfig{};
        canonicalizeDeviceId(id, d.deviceId, sizeof(d.deviceId));

        d.colorIndex = o["colorIndex"].is<uint8_t>() ? o["colorIndex"].as<uint8_t>() : TILT_NONE;
        if (d.colorIndex >= TILT_COLORS)
            d.colorIndex = TILT_NONE;

        if (o["friendlyName"].is<const char*>())
            strlcpy(d.friendlyName, o["friendlyName"], sizeof(d.friendlyName));
        if (o["googleSheetsName"].is<const char*>())
            strlcpy(d.googleSheetsName, o["googleSheetsName"], sizeof(d.googleSheetsName));
        if (o["modelLabel"].is<const char*>())
            strlcpy(d.modelLabel, o["modelLabel"], sizeof(d.modelLabel));
        if (o["notes"].is<const char*>())
            strlcpy(d.notes, o["notes"], sizeof(d.notes));
        if (o["gsheetsLink"].is<const char*>())
            strlcpy(d.gsheetsLink, o["gsheetsLink"], sizeof(d.gsheetsLink));

        d.enabled = o["enabled"].is<bool>() ? o["enabled"].as<bool>() : true;
        d.hasCalibration = o["hasCalibration"].is<bool>() ? o["hasCalibration"].as<bool>() : false;

        if (o["cal"].is<JsonObjectConst>()) {
            JsonObjectConst cal = o["cal"];
            if (cal["x0"].is<double>()) d.cal.x0 = cal["x0"].as<double>();
            if (cal["x1"].is<double>()) d.cal.x1 = cal["x1"].as<double>();
            if (cal["x2"].is<double>()) d.cal.x2 = cal["x2"].as<double>();
            if (cal["x3"].is<double>()) d.cal.x3 = cal["x3"].as<double>();
        }

        size_t a = 0;
        for (JsonVariantConst v : o["aliases"].as<JsonArrayConst>()) {
            if (a >= MAX_DEVICE_ALIASES)
                break;
            const char *alias = v.as<const char*>();
            if (isValidDeviceId(alias)) {
                canonicalizeDeviceId(alias, d.aliases[a], DEVICE_ID_LEN);
                a++;
            }
        }

        i++;
    }

    Log.info("Loaded %u device-specific Tilt configurations.\r\n", (unsigned)i);
    return true;
}

bool DeviceConfigStore::save() {
    JsonDocument doc;
    to_json(doc);

    size_t json_size = measureJson(doc);
    if (json_size == 0 || json_size > DEVICE_CONFIG_BUFFER_SIZE) {
        Log.error("Device config too large to save (%u bytes).\r\n", (unsigned)json_size);
        return false;
    }

    char *buffer = (char*)malloc(json_size + 1);
    if (buffer == nullptr)
        return false;

    serializeJson(doc, buffer, json_size + 1);

    FILE *f = fopen(DEVICE_CONFIG_FILE, "w");
    if (f == nullptr) {
        free(buffer);
        Log.error("Unable to write %s.\r\n", DEVICE_CONFIG_FILE);
        return false;
    }

    size_t written = fwrite(buffer, 1, json_size, f);
    fclose(f);
    free(buffer);

    return written == json_size;
}
