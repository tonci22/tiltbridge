#ifndef _JSONCONFIG_H
#define _JSONCONFIG_H

#include <ArduinoJson.h>
#include "tilt/tiltHydrometer.h"
#include "filesystem.h"
#include <stdio.h>

// CONFIG_DIR includes the filesystem mount point for ESP-IDF VFS compatibility
#define CONFIG_DIR FILESYSTEM_PREFIX "/conf"
#define JSON_CONFIG_FILE "tiltbridgeConfig.json"

// Accepted range for every per-target push interval, shared by the config loader and by the
// /api/settings/targets/ handler so a value the web UI accepts is never one the loader then
// throws away. 12 hours matches the ceiling legacyFermentrackPushEvery has always used.
//
// The 10-minute floor is a deliberate policy choice, not a technical limit: a Tilt's gravity
// moves far too slowly for anything faster to carry information, and every one of these targets
// is a remote HTTP endpoint where a faster cadence costs battery, heap and - for Google Sheets,
// which meters against a daily Apps Script execution-time quota - the ability to upload at all
// later in the day.
//
// It applies only to the targets converted from compile-time constants. The intervals that were
// already configurable (MQTT, Brewstatus, Taplist.io, InfluxDB, legacy Fermentrack) keep their
// own existing bounds: those are commonly pointed at a broker or a server on the local network,
// where a 30-second cadence is free and useful.
#define PUSH_EVERY_MIN_SEC 600
#define PUSH_EVERY_MAX_SEC 43200

struct TiltCalData {
    double x0 = 0.0;
    double x1 = 1.0;
    double x2 = 0.0;
    double x3 = 0.0;
};

struct GsheetsConfig {
    char name[26] = "";
    char link[256] = "";
};

struct GrainfatherURL {
    char link[65] = "";
};

class ConfigFile {
public:
    // This base already has virtual methods, so it needs a virtual destructor to be a
    // well-formed polymorphic base - without it, destroying a derived object through any
    // ConfigFile handle is undefined, and -Wdelete-non-virtual-dtor rightly refuses it.
    virtual ~ConfigFile() = default;

    // The following two functions when implemented should handle the derivation of the config filename as
    // well as then passing that filename to saveFile/loadFile
    bool save();
    bool load();
    bool deleteFile();

    // The following function can optionally be implemented to serve external json
    JsonDocument to_json_external();
    bool printConfig();
    bool printConfigFile();

protected:
    // The following two functions must be defined in inheriting classes
    virtual void load_from_json(JsonDocument obj);
    virtual JsonDocument to_json();

    bool deserializeConfig(const char *src);
    bool serializeConfig(FILE *dst);
    bool saveFile(const char * filename);
    bool loadFile(const char * filename);
    virtual bool getFilename(char *filename);
};

class Config: public ConfigFile {
public:
    char mdnsID[32] = "tiltbridge";
    char guid[17] = "";
    bool invertTFT = false;
    bool combineTilts = false;
    bool update_filesystem = false;
    int8_t TZoffset = -5;
    char tempUnit[2] = "F";
    char gravityUnit[3] = "SG";  // "SG" = Specific Gravity, "P" = Plato, "B" = Brix
    uint8_t smoothFactor = 60;
    bool applyCalibration = true;
    bool tempCorrect = false;

    // Outbound sender health / automatic recovery
    bool senderRecoveryEnabled = true;      // restart when outbound processing stops progressing

    /*
     * Whether to move off a poor access point when a materially better one is available on
     * the same network. Defaults on, matching senderRecoveryEnabled above: both trade a
     * brief, deliberate interruption for getting out of a state the device cannot otherwise
     * leave. Turn it off on a single-access-point network, where there is nothing to move to
     * and the scan is pure cost. See the rationale block in wifi_link.cpp.
     */
    bool wifiRoamEnabled = true;
    // Heartbeat age that counts as stalled (clamped 60..600). At the top of the spec's
    // 60-90s window because a single Google Sheets upload may legitimately hold the sender
    // for up to 60 s (two TLS handshakes via the Apps Script redirect).
    uint16_t senderStaleRebootSec = 90;

    // Persistent offline queue. Three intervals govern three different things and none of them
    // should be confused for another:
    //
    //   * queueSnapshotIntervalSec      - how often a reading is CAPTURED. take_queue_snapshot()
    //                                     walks every enabled Tilt, builds a QueuedReading and
    //                                     appends it to flash, so this is the sampling rate of
    //                                     the whole system: it decides how many rows ever exist,
    //                                     and therefore how many rows reach the spreadsheet.
    //                                     Because the append is what persists it, it also sets
    //                                     how much an unexpected reset can lose - but do not
    //                                     mistake it for a pure flash-wear knob. Raising it
    //                                     coarsens the data.
    //   * a target's <target>PushEvery  - how often whatever has been captured is UPLOADED to
    //                                     that target. This is latency and batching only; it
    //                                     cannot produce a row that was never captured.
    //   * maxQueuedRecords              - how much of an outage the queue can absorb.
    //
    // So the row cadence in the spreadsheet is the SNAPSHOT interval, and the push interval only
    // decides how long a captured row waits before it gets there. Shortening a push interval
    // does not sample more often; shortening the snapshot interval does not upload sooner.
    //
    // Changing it captures a reading immediately and restarts the cadence from that moment
    // (http_server sets queue_timer_restart_rqd; main.cpp turns that into a due snapshot), so
    // the effect of a change is visible at once instead of one interval later.
    bool offlineQueueEnabled = true;
    uint16_t queueSnapshotIntervalSec = 1800;   // 30 minutes
    uint16_t maxQueuedRecords = 1500;           // ~7 days at 4 Tilts / 30 min; 1500 * 128 B = 192 KB
    // Records per upload request. 10 rather than 20: on a classic ESP32 the mbedTLS
    // handshake to Google needs a large contiguous allocation, and a 4 KB payload from a
    // 20-record batch was enough to make it fail with MBEDTLS_ERR_X509_ALLOC_FAILED on a
    // fragmented heap. Raise it only if free heap and fragmentation allow.
    uint8_t queueBatchSize = 10;

    TiltCalData tilt_calibration[TILT_COLORS];
    GsheetsConfig gsheets_config[TILT_COLORS];
    GrainfatherURL grainfatherURL[TILT_COLORS];
    uint16_t grainfatherPushEvery = 900;    // 15 minutes; was GRAINFATHER_DELAY

    // Legacy Fermentrack Settings
    char legacyFermentrackURL[256] = "";
    uint16_t legacyFermentrackPushEvery = 30; 

    // Fermentrack 2 Settings
    char fermentrackHostname[128] = "";  // Hostname (or IP address) of the upstream server
    uint16_t fermentrackPort = 80;       // Port of the upstream server (defaults to 80)
    char fermentrackUsername[128] = "";  // Username for the upstream server. Deleted after registration.
    char fermentrackDeviceID[40] = "";   // UUID of this device, as assigned by upstream server
    char fermentrackAPIKey[40] = "";     // API key (uuid4 format) for the brewhouse this device is assigned to, as assigned by upstream server
    // 10 minutes. The compile-time FERMENTRACK_DELAY this replaces was 5 minutes, which is below
    // PUSH_EVERY_MIN_SEC - so unlike the other five targets, this default does change.
    uint16_t fermentrackPushEvery = 600;


    char brewstatusURL[256] = "";
    uint16_t brewstatusPushEvery = 30;
    char taplistioURL[256] = "";
    uint16_t taplistioPushEvery = 300;
    char scriptsURL[256] = "";
    char scriptsEmail[256] = "";
    // 10 minutes; was the compile-time GSCRIPTS_DELAY. Independent of
    // queueSnapshotIntervalSec above - this is how often a row reaches the spreadsheet.
    uint16_t gsheetsPushEvery = 600;
    // Enhanced batched Google Sheets protocol. On by default for this fork: it requires an
    // Apps Script implementing docs/phase1/APPS_SCRIPT_PROTOCOL.md, and without one nothing
    // is ever acknowledged and the queue simply grows. Set false to fall back to the
    // legacy single-reading path.
    bool gsheetsV2Enabled = true;
    char brewersFriendKey[65] = "";
    uint16_t brewersFriendPushEvery = 900;  // 15 minutes; was BREWERS_FRIEND_DELAY
    char brewfatherKey[65] = "";
    uint16_t brewfatherPushEvery = 900;     // 15 minutes; was BREWFATHER_DELAY
    char userTargetURL[129] = "";
    uint16_t userTargetPushEvery = 600;     // 10 minutes; was USER_TARGET_DELAY
    char mqttBrokerHost[256] = "";
    uint16_t mqttBrokerPort = 1883;
    char mqttUsername[51] = "";
    char mqttPassword[65] = "";
    char mqttTopic[31] = "";
    uint16_t mqttPushEvery = 30;

    // InfluxDB Settings
    char influxdbURL[256] = "";
    char influxdbToken[128] = "";
    char influxdbOrg[64] = "";
    char influxdbBucket[64] = "";
    uint16_t influxdbPushEvery = 900;


    JsonDocument to_json_external();
private:
    void load_from_json(JsonDocument obj);
    JsonDocument to_json();
    bool getFilename(char *filename);
};


extern Config config;
extern const size_t capacitySerial;
extern const size_t capacityDeserial;

#endif // _JSONCONFIG_H
