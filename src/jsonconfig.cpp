#include <thorlog.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "filesystem.h"  // Selects SPIFFS or LittleFS as necessary


#include "getGuid.h"
#include "serialhandler.h"

#include "jsonconfig.h"
#include "JsonKeys.h"
#include "bridge_lcd.h"  // for HAVE_LCD and HAVE_STATUS_LED  (note - HAVE_STATUS_LED is not used anywhere)
#include "targets/fermentrack_2.h"  // For fermentrackRegistrationError


#define MAX_FILENAME_LENGTH  48
#define JSON_CONFIG_BUFFER_SIZE 8192


Config config;
const char *filename = JSON_CONFIG_FILE;
const size_t capacitySerial = 6152;
const size_t capacityDeserial = 8192;

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

    if (size == 0 || size > JSON_CONFIG_BUFFER_SIZE) {
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
    size_t bytes_read = fread(buffer, 1, size, file);
    fclose(file);

    if (bytes_read != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}


bool ConfigFile::loadFile(const char * filename) {

    // Loads the configuration from a file on FILESYSTEM
    if (!filesystem_exists(filename)) {
        // File does not exist
        Log.info("Config file %s does not exist - creating with defaults\r\n", filename);
        saveFile(filename);
    } else {
        // Existing configuration present
        Log.verbose("Found existing config file %s\r\n", filename);
    }

    // Read file into buffer
    size_t size;
    char* buffer = read_file_to_buffer(filename, &size);
    if (buffer == NULL) {
        Log.error("Unable to access config file %s\r\n", filename);
        return false;
    }

    bool result = deserializeConfig(buffer);
    free(buffer);
    return result;
}

bool ConfigFile::saveFile(const char * filename) {
    // Saves the configuration to a file on FILESYSTEM
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return false;
    }

    // Serialize JSON to file
    if (!serializeConfig(file)) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

bool ConfigFile::deserializeConfig(const char *src) {
    // Deserialize configuration
    JsonDocument doc;

    // Parse the JSON object from the buffer
    DeserializationError err = deserializeJson(doc, src);

    if (err) {
        load_from_json(doc);
        return true;
    } else {
        load_from_json(doc);
        return true;
    }
}

bool ConfigFile::serializeConfig(FILE *dst) {
    // Serialize configuration
    JsonDocument doc = to_json();

    // Measure required size
    size_t json_size = measureJson(doc);
    if (json_size == 0 || json_size > JSON_CONFIG_BUFFER_SIZE) {
        return false;
    }

    // Allocate buffer
    char* buffer = (char*)malloc(json_size + 1);
    if (!buffer) {
        return false;
    }

    // Serialize to buffer
    size_t written = serializeJson(doc, buffer, json_size + 1);

    // Write to file
    size_t file_written = fwrite(buffer, 1, written, dst);
    free(buffer);

    return file_written == written;
}

JsonDocument ConfigFile::to_json_external() {
    return to_json();
}

bool ConfigFile::save() {
    char filename[MAX_FILENAME_LENGTH];
    if(!getFilename(filename))
        return false;
    return saveFile(filename);
}

bool ConfigFile::load() {
    char filename[MAX_FILENAME_LENGTH];
    if(!getFilename(filename))
        return false;
    return loadFile(filename);
}

bool ConfigFile::deleteFile() {
    char filename[MAX_FILENAME_LENGTH];
    if(!getFilename(filename))
        return false;
    return remove(filename) == 0;
}

bool ConfigFile::printConfig() {
    // Serialize configuration
    JsonDocument doc = to_json();

    // Serialize to buffer and print
    size_t json_size = measureJson(doc);
    char* buffer = (char*)malloc(json_size + 1);
    if (!buffer) {
        return false;
    }

    serializeJson(doc, buffer, json_size + 1);
    printf("%s", buffer);
    free(buffer);

    printCR(true);
    return true;
}

bool ConfigFile::printConfigFile() {
    // Prints the content of a file to the Serial
    char filename[MAX_FILENAME_LENGTH];
    if(!getFilename(filename))
        return false;

    size_t size;
    char* buffer = read_file_to_buffer(filename, &size);
    if (buffer == NULL) {
        return false;
    }

    // Print the buffer
    printf("%s", buffer);
    free(buffer);

    printCR(true);
    return true;
}

bool Config::getFilename(char *filename) {
    // Build the filename from the prefix & config file name
    snprintf(filename, MAX_FILENAME_LENGTH, "%s/%s", CONFIG_DIR, JSON_CONFIG_FILE);
    return true;
}

JsonDocument Config::to_json_external() {
    // This function generates the JSON document that gets served externally. Can add keys here that we don't
    // save as part of the configuration.
    JsonDocument obj = to_json();

#if HAVE_LCD
    obj["have_lcd"] = true;
#else
    obj["have_lcd"] = false;
#endif

// Note - HAVE_STATUS_LED is not actually defined/used anywhere
#if HAVE_STATUS_LED
    obj["have_led"] = true;
#else
    obj["have_led"] = false;
#endif

    // Fermentrack 2 registration status
    obj[Fermentrack2SettingsKeys::fermentrackRegistrationError] = (uint8_t) fermentrackRegistrationError;

    return obj;
}

JsonDocument Config::to_json() {
    JsonDocument obj;

    obj["mdnsID"] = mdnsID;
    obj["guid"] = guid;
    obj["invertTFT"] = invertTFT;
    obj["combineTilts"] = combineTilts;
    obj["update_filesystem"] = update_filesystem;
    obj["TZoffset"] = TZoffset;
    obj["tempUnit"] = tempUnit;
    obj[GeneralSettings::gravityUnit] = gravityUnit;
    obj["smoothFactor"] = smoothFactor;
    obj["applyCalibration"] = applyCalibration;
    obj["tempCorrect"] = tempCorrect;
    obj["senderRecoveryEnabled"] = senderRecoveryEnabled;
    obj["senderStaleRebootSec"] = senderStaleRebootSec;

    obj[QueueSettings::offlineQueueEnabled] = offlineQueueEnabled;
    obj[QueueSettings::queueSnapshotIntervalSec] = queueSnapshotIntervalSec;
    obj[QueueSettings::maxQueuedRecords] = maxQueuedRecords;
    obj[QueueSettings::queueBatchSize] = queueBatchSize;

    for(int x=0;x<TILT_COLORS;x++) {
        obj[tilt_color_names[x]]["x0"] = tilt_calibration[x].x0;
        obj[tilt_color_names[x]]["x1"] = tilt_calibration[x].x1;
        obj[tilt_color_names[x]]["x2"] = tilt_calibration[x].x2;
        obj[tilt_color_names[x]]["x3"] = tilt_calibration[x].x3;

        obj[tilt_color_names[x]]["name"] = gsheets_config[x].name;
        obj[tilt_color_names[x]]["link"] = gsheets_config[x].link;

        obj[tilt_color_names[x]]["grainfatherURL"] = grainfatherURL[x].link;
    }

    // Legacy Fermentrack Settings
    obj[FermentrackSettings::legacyFermentrackURL] = legacyFermentrackURL;
    obj[FermentrackSettings::legacyFermentrackPushEvery] = legacyFermentrackPushEvery;

    // Fermentrack 2 Settings
    obj[FermentrackSettings::fermentrackHostname] = fermentrackHostname;
    obj[FermentrackSettings::fermentrackPort] = fermentrackPort;
    obj[FermentrackSettings::fermentrackUsername] = fermentrackUsername;
    obj[FermentrackSettings::fermentrackDeviceID] = fermentrackDeviceID;
    obj[FermentrackSettings::fermentrackAPIKey] = fermentrackAPIKey;
    obj[FermentrackSettings::fermentrackPushEvery] = fermentrackPushEvery;

    obj[BrewstatusSettings::brewstatusURL] = brewstatusURL;
    obj[BrewstatusSettings::brewstatusPushEvery] = brewstatusPushEvery;
    obj[TaplistioSettings::taplistioURL] = taplistioURL;
    obj[TaplistioSettings::taplistioPushEvery] = taplistioPushEvery;
    obj[GoogleSheetsSettings::scriptsURL] = scriptsURL;
    obj[GoogleSheetsSettings::scriptsEmail] = scriptsEmail;
    obj[GoogleSheetsSettings::gsheetsV2Enabled] = gsheetsV2Enabled;
    obj[GoogleSheetsSettings::gsheetsPushEvery] = gsheetsPushEvery;
    obj[BrewersFriendSettings::brewersFriendKey] = brewersFriendKey;
    obj[BrewersFriendSettings::brewersFriendPushEvery] = brewersFriendPushEvery;
    obj[BrewfatherSettings::brewfatherKey] = brewfatherKey;
    obj[BrewfatherSettings::brewfatherPushEvery] = brewfatherPushEvery;
    obj[UserTargetSettings::userTargetURL] = userTargetURL;
    obj[UserTargetSettings::userTargetPushEvery] = userTargetPushEvery;
    obj[GrainfatherSettings::grainfatherPushEvery] = grainfatherPushEvery;
    obj[MQTTSettings::mqttBrokerHost] = mqttBrokerHost;
    obj[MQTTSettings::mqttBrokerPort] = mqttBrokerPort;
    obj[MQTTSettings::mqttUsername] = mqttUsername;
    obj[MQTTSettings::mqttPassword] = mqttPassword;
    obj[MQTTSettings::mqttTopic] = mqttTopic;
    obj[MQTTSettings::mqttPushEvery] = mqttPushEvery;

    // InfluxDB Settings
    obj[InfluxDBSettings::influxdbURL] = influxdbURL;
    obj[InfluxDBSettings::influxdbToken] = influxdbToken;
    obj[InfluxDBSettings::influxdbOrg] = influxdbOrg;
    obj[InfluxDBSettings::influxdbBucket] = influxdbBucket;
    obj[InfluxDBSettings::influxdbPushEvery] = influxdbPushEvery;

    return obj;
}

/**
 * @brief Load one target's push interval, clamping anything out of range back to its default.
 *
 * A push interval says how often a reading is UPLOADED to one target. It is unrelated to
 * queueSnapshotIntervalSec, which says how often the offline queue is written to flash - see
 * the block comment on those fields in jsonconfig.h.
 *
 * An absent key leaves the field alone, so a partial config never resets an interval, and an
 * out-of-range one falls back to the default rather than to the previous value: a config file
 * that has been hand-edited into nonsense should land somewhere predictable.
 */
static void loadPushEvery(const JsonDocument& obj, const char* key, uint16_t& field,
                          uint16_t minSeconds, uint16_t maxSeconds, uint16_t defaultSeconds) {
    if (obj[key].isNull())
        return;

    const int value = obj[key].as<int>();

    if (value < minSeconds || value > maxSeconds) {
        field = defaultSeconds;
        return;
    }

    field = static_cast<uint16_t>(value);
}

void Config::load_from_json(JsonDocument obj) {
    // Load all config objects
    //
    if (!obj["mdnsID"].isNull()) {
        const char *md = obj["mdnsID"];
        strlcpy(mdnsID, md, 32);  // TODO - Change all of these to use 'sizeof' instead of hard-coded lengths
    }

//    if (!obj["guid"].isNull()) {
//        const char *gd = obj["guid"];
//        strlcpy(guid, gd, sizeof(guid));
//    } else {
    // Always regenerate the guid
    char newguid[sizeof(guid)];
    getGuid(newguid);
    strlcpy(guid, newguid, sizeof(guid));
//    }

    if (!obj["invertTFT"].isNull()) {
        invertTFT = obj["invertTFT"];
    }

    if (!obj["combineTilts"].isNull()) {
        combineTilts = obj["combineTilts"];
    }

    if (!obj["update_filesystem"].isNull()) {
        update_filesystem = obj["update_filesystem"];
    }

    if (!obj["TZoffset"].isNull()) {
        TZoffset = int(obj["TZoffset"]);
    }

    if (!obj["tempUnit"].isNull()) {
        const char *tu = obj["tempUnit"];
        strlcpy(tempUnit, tu, 2);
    }

    if (!obj[GeneralSettings::gravityUnit].isNull()) {
        const char *gu = obj[GeneralSettings::gravityUnit];
        if (strcmp(gu, "SG") == 0 || strcmp(gu, "P") == 0 || strcmp(gu, "B") == 0) {
            strlcpy(gravityUnit, gu, sizeof(gravityUnit));
        }
    }

    if (!obj["smoothFactor"].isNull()) {
        smoothFactor = int(obj["smoothFactor"]);
    }

    if (!obj["applyCalibration"].isNull()) {
        applyCalibration = obj["applyCalibration"];
    }

    if (!obj["tempCorrect"].isNull()) {
        tempCorrect = obj["tempCorrect"];
    }

    if (!obj["senderRecoveryEnabled"].isNull()) {
        senderRecoveryEnabled = obj["senderRecoveryEnabled"];
    }

    if (!obj["senderStaleRebootSec"].isNull()) {
        senderStaleRebootSec = int(obj["senderStaleRebootSec"]);

        // Below 60s risks tripping on a legitimately slow pass (Fermentrack's three
        // sequential requests plus Google's 10s budget); above 600s stops being a recovery.
        if (senderStaleRebootSec < 60 || senderStaleRebootSec > 600) {
            senderStaleRebootSec = 75;
        }
    }

    // Offline queue. Every bound is clamped here so a hand-edited config cannot brick the
    // device or wear out flash.
    if (!obj[QueueSettings::offlineQueueEnabled].isNull()) {
        offlineQueueEnabled = obj[QueueSettings::offlineQueueEnabled];
    }

    if (!obj[QueueSettings::queueSnapshotIntervalSec].isNull()) {
        queueSnapshotIntervalSec = int(obj[QueueSettings::queueSnapshotIntervalSec]);

        // Floor of 60s protects flash; ceiling of 6h keeps the queue meaningful.
        if (queueSnapshotIntervalSec < 60 || queueSnapshotIntervalSec > 21600) {
            queueSnapshotIntervalSec = 1800;
        }
    }

    if (!obj[QueueSettings::maxQueuedRecords].isNull()) {
        maxQueuedRecords = int(obj[QueueSettings::maxQueuedRecords]);

        // 3000 records * 128 B = 384 KB, which still leaves room on the LittleFS partition.
        if (maxQueuedRecords < 100 || maxQueuedRecords > 3000) {
            maxQueuedRecords = 1500;
        }
    }

    if (!obj[QueueSettings::queueBatchSize].isNull()) {
        queueBatchSize = int(obj[QueueSettings::queueBatchSize]);

        if (queueBatchSize < 1 || queueBatchSize > 50) {
            queueBatchSize = 20;
        }
    }

    // Loop through everything that is a "tilt-specific" setting
    for(int x=0;x<TILT_COLORS;x++) {
        // Calibration points
        if (!obj[tilt_color_names[x]]["x0"].isNull()) {
            tilt_calibration[x].x0 = float(obj[tilt_color_names[x]]["x0"]);
        }

        if (!obj[tilt_color_names[x]]["x1"].isNull()) {
            tilt_calibration[x].x1 = float(obj[tilt_color_names[x]]["x1"]);
        }

        if (!obj[tilt_color_names[x]]["x2"].isNull()) {
            tilt_calibration[x].x2 = float(obj[tilt_color_names[x]]["x2"]);
        }

        if (!obj[tilt_color_names[x]]["x3"].isNull()) {
            tilt_calibration[x].x3 = float(obj[tilt_color_names[x]]["x3"]);
        }

        // GSheet Info
        if (!obj[tilt_color_names[x]]["name"].isNull()) {
            const char *sn = obj[tilt_color_names[x]]["name"];
            strlcpy(gsheets_config[x].name, sn, 25);
        }

        if (!obj[tilt_color_names[x]]["link"].isNull()) {
            const char *sn = obj[tilt_color_names[x]]["link"];
            strlcpy(gsheets_config[x].link, sn, 255);
        }

        // Grainfather URLs
        if (!obj[tilt_color_names[x]]["grainfatherURL"].isNull()) {
            const char *sn = obj[tilt_color_names[x]]["grainfatherURL"];
            strlcpy(grainfatherURL[x].link, sn, 64);
        }
    } // End Tilt-specific config loop


    // Legacy Fermentrack Settings
    if (!obj[FermentrackSettings::legacyFermentrackURL].isNull()) {
        const char *tu = obj[FermentrackSettings::legacyFermentrackURL];
        strlcpy(legacyFermentrackURL, tu, 256);
    }

    if (!obj[FermentrackSettings::legacyFermentrackPushEvery].isNull()) {
        legacyFermentrackPushEvery = int(obj[FermentrackSettings::legacyFermentrackPushEvery]);

        if (legacyFermentrackPushEvery < 30 || legacyFermentrackPushEvery > 43200) {
            legacyFermentrackPushEvery = 60;
        }
    }

    // Fermentrack 2 Settings
    if (!obj[FermentrackSettings::fermentrackHostname].isNull()) {
        const char *tu = obj[FermentrackSettings::fermentrackHostname];
        strlcpy(fermentrackHostname, tu, sizeof(fermentrackHostname));
    }

    if (!obj[FermentrackSettings::fermentrackPort].isNull()) {
        int port = int(obj[FermentrackSettings::fermentrackPort]);

        if (port < 0 || port > 65535) {
            fermentrackPort = 80;
        } else {
            fermentrackPort = static_cast<uint16_t>(port);
        }
    }

    if (!obj[FermentrackSettings::fermentrackUsername].isNull()) {
        const char *tu = obj[FermentrackSettings::fermentrackUsername];
        strlcpy(fermentrackUsername, tu, sizeof(fermentrackUsername));
    }

    if (!obj[FermentrackSettings::fermentrackDeviceID].isNull()) {
        const char *tu = obj[FermentrackSettings::fermentrackDeviceID];
        strlcpy(fermentrackDeviceID, tu, sizeof(fermentrackDeviceID));
    }

    if (!obj[FermentrackSettings::fermentrackAPIKey].isNull()) {
        const char *tu = obj[FermentrackSettings::fermentrackAPIKey];
        strlcpy(fermentrackAPIKey, tu, sizeof(fermentrackAPIKey));
    }

    loadPushEvery(obj, FermentrackSettings::fermentrackPushEvery, fermentrackPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 600);


    // BrewStatus Settings
    if (!obj[BrewstatusSettings::brewstatusURL].isNull()) {
        const char *bu = obj[BrewstatusSettings::brewstatusURL];
        strlcpy(brewstatusURL, bu, 256);
    }

    if (!obj[BrewstatusSettings::brewstatusPushEvery].isNull()) {
        int pe = obj[BrewstatusSettings::brewstatusPushEvery];
        brewstatusPushEvery = pe;
    }

    // TaplistIO Settings
    if (!obj[TaplistioSettings::taplistioURL].isNull()) {
        const char *tu = obj[TaplistioSettings::taplistioURL];
        strlcpy(taplistioURL, tu, 256);
    }

    if (!obj[TaplistioSettings::taplistioPushEvery].isNull()) {
        taplistioPushEvery = obj[TaplistioSettings::taplistioPushEvery];
    }

    // Google Scripts Settings
    if (!obj[GoogleSheetsSettings::scriptsURL].isNull()) {
        const char *su = obj[GoogleSheetsSettings::scriptsURL];
        strlcpy(scriptsURL, su, 256);
    }

    if (!obj[GoogleSheetsSettings::scriptsEmail].isNull()) {
        const char *se = obj[GoogleSheetsSettings::scriptsEmail];
        strlcpy(scriptsEmail, se, 256);
    }

    if (!obj[GoogleSheetsSettings::gsheetsV2Enabled].isNull()) {
        gsheetsV2Enabled = obj[GoogleSheetsSettings::gsheetsV2Enabled];
    }

    loadPushEvery(obj, GoogleSheetsSettings::gsheetsPushEvery, gsheetsPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 600);

    // Brewers Friend
    if (!obj[BrewersFriendSettings::brewersFriendKey].isNull()) {
        const char *bf = obj[BrewersFriendSettings::brewersFriendKey];
        strlcpy(brewersFriendKey, bf, 65);
    }

    loadPushEvery(obj, BrewersFriendSettings::brewersFriendPushEvery, brewersFriendPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 900);

    // Brewfather
    if (!obj[BrewfatherSettings::brewfatherKey].isNull()) {
        const char *bk = obj[BrewfatherSettings::brewfatherKey];
        strlcpy(brewfatherKey, bk, 65);
    }

    loadPushEvery(obj, BrewfatherSettings::brewfatherPushEvery, brewfatherPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 900);

    loadPushEvery(obj, GrainfatherSettings::grainfatherPushEvery, grainfatherPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 900);

    // User-defined Target Settings
    if (!obj[UserTargetSettings::userTargetURL].isNull()) {
        const char *uturl = obj[UserTargetSettings::userTargetURL];
        strlcpy(userTargetURL, uturl, 128);
    }

    loadPushEvery(obj, UserTargetSettings::userTargetPushEvery, userTargetPushEvery,
                  PUSH_EVERY_MIN_SEC, PUSH_EVERY_MAX_SEC, 600);

    // MQTT Settings
    if (!obj[MQTTSettings::mqttBrokerHost].isNull()) {
        const char *mi = obj[MQTTSettings::mqttBrokerHost];
        strlcpy(mqttBrokerHost, mi, 256);
    }

    if (!obj[MQTTSettings::mqttBrokerPort].isNull()) {
        mqttBrokerPort = int(obj[MQTTSettings::mqttBrokerPort]);
    }

    if (!obj[MQTTSettings::mqttUsername].isNull()) {
        const char *mu = obj[MQTTSettings::mqttUsername];
        strlcpy(mqttUsername, mu, 51);
    }

    if (!obj[MQTTSettings::mqttPassword].isNull()) {
        const char *mp = obj[MQTTSettings::mqttPassword];
        strlcpy(mqttPassword, mp, 65);
    }

    if (!obj[MQTTSettings::mqttTopic].isNull()) {
        const char *mt = obj[MQTTSettings::mqttTopic];
        strlcpy(mqttTopic, mt, 31);
    }

    if (!obj[MQTTSettings::mqttPushEvery].isNull()) {
        mqttPushEvery = int(obj[MQTTSettings::mqttPushEvery]);
    }

    // InfluxDB Settings
    if (!obj[InfluxDBSettings::influxdbURL].isNull()) {
        const char *iu = obj[InfluxDBSettings::influxdbURL];
        strlcpy(influxdbURL, iu, 256);
    }

    if (!obj[InfluxDBSettings::influxdbToken].isNull()) {
        const char *it = obj[InfluxDBSettings::influxdbToken];
        strlcpy(influxdbToken, it, 128);
    }

    if (!obj[InfluxDBSettings::influxdbOrg].isNull()) {
        const char *io = obj[InfluxDBSettings::influxdbOrg];
        strlcpy(influxdbOrg, io, 64);
    }

    if (!obj[InfluxDBSettings::influxdbBucket].isNull()) {
        const char *ib = obj[InfluxDBSettings::influxdbBucket];
        strlcpy(influxdbBucket, ib, 64);
    }

    if (!obj[InfluxDBSettings::influxdbPushEvery].isNull()) {
        influxdbPushEvery = int(obj[InfluxDBSettings::influxdbPushEvery]);
    }
}
