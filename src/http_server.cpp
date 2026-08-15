#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <ArduinoJson.h>
#include <new>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "url_utils.h"
#include "thorlog.h"

#include "resetreasons.h"
#include "uptime.h"
#include "version.h"
#include "jsonconfig.h"
#include "tilt/tiltScanner.h"
#include "sendData.h"
#include "sender_health.h"
#include "device_config.h"
#include "queue/reading_queue.h"
#include "JsonKeys.h"

#include <esp_wifi_config.h>

#include "http_server.h"
#include "idf_http_server.h"
#include "idf_json_utils.h"
#include "idf_static_files.h"
#include "http_calibration.h"
#include "targets/fermentrack_2.h"

static const char *TAG = "http_server";

httpServer http_server;

// Helper function to clear name reset flag without requiring http_server.h include
// This avoids ESPAsyncWebServer/ESP-IDF http_parser.h enum conflicts
void http_server_clear_name_reset() {
    http_server.name_reset_requested = false;
}

// Timer handles for triggering immediate sends from HTTP configuration updates
static TimerHandle_t sendNowLegacyFTTimer = nullptr;
static TimerHandle_t sendNowFTTimer = nullptr;
static TimerHandle_t sendNowGSheetsTimer = nullptr;
static TimerHandle_t sendNowBrewersFriendTimer = nullptr;
static TimerHandle_t sendNowBrewfatherTimer = nullptr;
static TimerHandle_t sendNowUserTargetTimer = nullptr;
static TimerHandle_t sendNowGrainfatherTimer = nullptr;
static TimerHandle_t sendNowBrewStatusTimer = nullptr;
static TimerHandle_t sendNowTaplistioTimer = nullptr;
static TimerHandle_t sendNowMqttTimer = nullptr;
static TimerHandle_t sendNowInfluxdbTimer = nullptr;

// Timer callbacks for send-now triggers
static void sendNowLegacyFTCallback(TimerHandle_t xTimer) { data_sender.send_legacy_fermentrack = true; }
static void sendNowFTCallback(TimerHandle_t xTimer) { data_sender.send_fermentrack = true; }
static void sendNowGSheetsCallback(TimerHandle_t xTimer) { data_sender.send_gSheets = true; }
static void sendNowBrewersFriendCallback(TimerHandle_t xTimer) { data_sender.send_brewersFriend = true; }
static void sendNowBrewfatherCallback(TimerHandle_t xTimer) { data_sender.send_brewfather = true; }
static void sendNowUserTargetCallback(TimerHandle_t xTimer) { data_sender.send_userTarget = true; }
static void sendNowGrainfatherCallback(TimerHandle_t xTimer) { data_sender.send_grainfather = true; }
static void sendNowBrewStatusCallback(TimerHandle_t xTimer) { data_sender.send_brewStatus = true; }
static void sendNowTaplistioCallback(TimerHandle_t xTimer) { data_sender.send_taplistio = true; }
static void sendNowMqttCallback(TimerHandle_t xTimer) { data_sender.send_mqtt = true; }
static void sendNowInfluxdbCallback(TimerHandle_t xTimer) { data_sender.send_influxdb = true; }

// Helper to start a send-now timer (creates if needed, then starts)
static void startSendNowTimer(TimerHandle_t& timer, const char* name, TimerCallbackFunction_t callback, uint32_t delaySecs) {
    if (timer == nullptr) {
        timer = xTimerCreate(name, pdMS_TO_TICKS(delaySecs * 1000), pdFALSE, nullptr, callback);
    } else {
        xTimerChangePeriod(timer, pdMS_TO_TICKS(delaySecs * 1000), 0);
    }
    if (timer != nullptr) {
        xTimerStart(timer, 0);
    }
}


//=============================================================================
// JSON Response Generators (GET handlers)
//=============================================================================

static void http_json(JsonDocument &doc) {
    doc["tilts"] = tilt_scanner.tilt_to_json();
    doc[GeneralSettings::gravityUnit] = config.gravityUnit;
}

static void settings_json(JsonDocument &doc) {
    doc = config.to_json_external();
}

static void this_version(JsonDocument &doc) {
    doc["version"] = version();
    doc["branch"] = branch();
    doc["build"] = build();
    doc["hardware"] = hardware();
}

static void uptime_json(JsonDocument &doc) {
    const int days = uptimeDays();
    const int hours = uptimeHours();
    const int minutes = uptimeMinutes();
    const int seconds = uptimeSeconds();
    const int millis = uptimeMillis();

    doc["days"] = days;
    doc["hours"] = hours;
    doc["minutes"] = minutes;
    doc["seconds"] = seconds;
    doc["millis"] = millis;
}

static void heap_json(JsonDocument &doc) {
    const uint32_t free = esp_get_free_heap_size();
    const uint32_t max = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const uint8_t frag = (free > 0) ? (100 - (max * 100) / free) : 0;

    doc["free"] = free;
    doc["max"] = max;
    doc["frag"] = frag;
}

static void reset_reason_json(JsonDocument &doc) {
    const int reset = (int)esp_reset_reason();

    doc["reason"] = resetReason[reset];
    doc["description"] = resetDescription[reset];
}

static void errors_json(JsonDocument &doc) {
    for (uint8_t i = 0; i < TARGET_COUNT; i++) {
        JsonObject target = doc[sendTargetNames[i]].to<JsonObject>();
        target["error_code"] = (uint8_t)data_sender.targetStatus[i].lastError;
        target["last_attempt_at"] = data_sender.targetStatus[i].lastAttemptTime;
    }
}

static void sender_json(JsonDocument &doc) {
    sender_health.to_json(doc);
}

static void devices_json(JsonDocument &doc) {
    device_config.to_json(doc);
}

static const char* queueUploadStateName(dataSendHandler::QueueUploadState s) {
    switch (s) {
        case dataSendHandler::QueueUploadState::SENDING:  return "SENDING";
        case dataSendHandler::QueueUploadState::RETRYING: return "RETRYING";
        case dataSendHandler::QueueUploadState::DISABLED: return "DISABLED";
        default:                                          return "IDLE";
    }
}

static void queue_json(JsonDocument &doc) {
    reading_queue.to_json(doc);

    doc["uploadStatus"] = queueUploadStateName(data_sender.queueUploadState);

    /*
     * What the flash can actually hold, so the UI stops offering a maxQueuedRecords the
     * device cannot honour. 0 means the filesystem could not be queried; the UI should fall
     * back to the static ceiling rather than showing "0 supported".
     */
    const uint16_t supported = reading_queue.storageCapacityRecords();
    doc["maxRecordsSupported"] = supported;

    /*
     * How long a total outage would take to fill the queue, which is the number that actually
     * matters: it is how long you have to notice before the oldest readings start being
     * dropped. Depends on the Tilt count and the persistence interval, neither of which the
     * queue itself knows about.
     */
    uint16_t activeTilts = 0;
    for (tiltHydrometer &th : tilt_scanner.m_tilt_devices) {
        if (device_config.isEnabled(th.deviceId()) && th.latest_gravity_value() != 0)
            activeTilts++;
    }

    doc["activeTilts"] = activeTilts;

    uint16_t cap = config.maxQueuedRecords;
    if (supported > 0 && supported < cap)
        cap = supported;

    if (activeTilts > 0 && config.queueSnapshotIntervalSec > 0) {
        const float recordsPerHour = (float)activeTilts * 3600.0f / (float)config.queueSnapshotIntervalSec;
        doc["estimatedRunwayHours"] = (float)cap / recordsPerHour;
    } else {
        doc["estimatedRunwayHours"] = nullptr;
    }


    if (data_sender.lastQueueUploadSuccessMs != 0)
        doc["lastUploadSuccessAgeSec"] = (sh_millis() - data_sender.lastQueueUploadSuccessMs) / 1000;
    else
        doc["lastUploadSuccessAgeSec"] = nullptr;
}


//=============================================================================
// Settings Update Handlers (PUT/POST handlers)
//=============================================================================

static bool updateJsonSettingBool(const JsonDocument& json, const char* key, bool& configVar) {
    if (json[key].is<bool>()) {
        configVar = json[key].as<bool>();
        if(json[key].as<bool>())
            Log.notice("Settings update, [%s]:(True) applied.\r\n", key);
        else
            Log.notice("Settings update, [%s]:(False) applied.\r\n", key);
        return true;
    }
    return false;
}

static bool updateJsonSetting(const JsonDocument& json, const char* key, char* configVar, uint16_t maxLen) {
    if (json[key].is<const char*>()) {
        if(strlen(json[key].as<const char*>()) > maxLen) {
            Log.warning("Settings update error, [%s]:(%s) too long.\r\n", key, json[key].as<const char*>());
        } else {
            strlcpy(configVar, json[key].as<const char*>(), maxLen);
            Log.notice("Settings update, [%s]:(%s) applied.\r\n", key, json[key].as<const char*>());
            return true;
        }
    } else {
        Log.warning("Settings update error, [%s]:(%s) not valid.\r\n", key, json[key].as<const char*>());
    }
    return false;
}

static bool updateJsonSetting(const JsonDocument& json, const char* key, uint16_t& configVar) {
    if (json[key].is<uint16_t>()) {
        configVar = json[key].as<uint16_t>();
        Log.notice("Settings update, [%s]:(%d) applied.\r\n", key, json[key].as<uint16_t>());
        return true;
    } else {
        Log.warning("Settings update error, [%s]:(%s) not valid.\r\n", key, json[key].as<const char*>());
    }
    return false;
}

/*
 * Delay used when a shortened push interval is already overdue at the moment it is saved.
 * Short enough to feel immediate, long enough that the upload cannot race config.save().
 */
#define PUSH_INTERVAL_DUE_NOW_SEC 5

/**
 * @brief Apply an optional per-target push interval - how often a reading is UPLOADED there.
 *
 * Absent means "leave it alone", unlike updateJsonSetting(), so one target's panel saving its
 * own fields never disturbs another's interval. Out of range is refused rather than silently
 * clamped, so the UI is told the value did not take.
 *
 * Not to be confused with queueSnapshotIntervalSec, which is how often the offline queue is
 * written to flash. That one is device-wide and lives with the controller settings.
 */
static bool applyPushEvery(const JsonDocument& json, const char* key, uint16_t& configVar,
                           uint16_t minSeconds = PUSH_EVERY_MIN_SEC) {
    if(!json[key].is<int32_t>())
        return true;

    // Read wider than the field so a value too large to be a uint16_t is REPORTED as out of
    // range rather than silently failing is<uint16_t>() and looking like an absent key.
    const int32_t value = json[key].as<int32_t>();

    if(value < minSeconds || value > PUSH_EVERY_MAX_SEC) {
        Log.warning("Settings update error, [%s]:(%d) outside %d..%d.\r\n",
                    key, (int)value, (int)minSeconds, PUSH_EVERY_MAX_SEC);
        return false;
    }

    configVar = (uint16_t)value;
    Log.notice("Settings update, [%s]:(%d) applied.\r\n", key, (int)value);
    return true;
}

static bool processTiltBridgeSettingsJson(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;
    bool hostnamechanged = false;

    // mDNS ID
    if(json["mdnsID"].is<const char*>()) {
        if (!isValidLabel(json["mdnsID"].as<const char*>())) {
            Log.warning("Settings update error, [mdnsID]:(%s) not valid.\r\n", json["mdnsID"]);
            failCount++;
        } else {
            if (strcmp(config.mdnsID, json["mdnsID"].as<const char*>()) != 0) {
                hostnamechanged = true;
                strlcpy(config.mdnsID, json["mdnsID"].as<const char*>(), 32);
                Log.notice("Settings update, [mdnsID]:(%s) applied.\r\n", json["mdnsID"].as<const char*>());
            } else {
                Log.notice("Settings update, [mdnsID]:(%s) NOT applied - no change.\r\n", json["mdnsID"].as<const char*>());
            }
        }
    }

    // tzOffset. Accepted under BOTH spellings, deliberately.
    //
    // The web UI has always sent "tzOffset", but /api/settings/json/ serves the field as
    // "TZoffset" - that is its name in Config::to_json() and in the config file on flash.
    // A client that GETs the settings and PUTs them back therefore had its timezone silently
    // dropped, reset to the -5 default on the next load, and was still told {"status":"ok"}.
    // Backup-and-restore is exactly that round trip.
    //
    // "tzOffset" stays the canonical spelling for input; "TZoffset" is accepted so a served
    // document is valid input to the endpoint that served it.
    JsonVariantConst tzOffsetValue =
        json["tzOffset"].isNull() ? json["TZoffset"] : json["tzOffset"];

    // Read as int rather than int8_t so a wildly out-of-range value is REPORTED rather than
    // failing the type test and looking like an absent key.
    if(tzOffsetValue.is<int>()) {
        const int tzOffset = tzOffsetValue.as<int>();
        if(tzOffset < -12 || tzOffset > 14) {
            Log.warning("Settings update error, [tzOffset]:(%d) not valid.\r\n", tzOffset);
            failCount++;
        } else {
            config.TZoffset = (int8_t)tzOffset;
            Log.notice("Settings update, [tzOffset]:(%d) applied.\r\n", tzOffset);
        }
    }

    // tempUnit
    if(json["tempUnit"].is<const char*>()) {
        if(strcmp(json["tempUnit"].as<const char*>(), "C") != 0 && strcmp(json["tempUnit"].as<const char*>(), "F") != 0) {
            Log.warning("Settings update error, [tempUnit]:(%s) not valid.\r\n", json["tempUnit"].as<const char*>());
        } else {
            strlcpy(config.tempUnit, json["tempUnit"].as<const char*>(), 2);
            Log.notice("Settings update, [tempUnit]:(%s) applied.\r\n", json["tempUnit"].as<const char*>());
        }
    }

    // gravityUnit
    if(json[GeneralSettings::gravityUnit].is<const char*>()) {
        const char* gu = json[GeneralSettings::gravityUnit].as<const char*>();
        if(strcmp(gu, "SG") != 0 && strcmp(gu, "P") != 0 && strcmp(gu, "B") != 0) {
            Log.warning("Settings update error, [gravityUnit]:(%s) not valid.\r\n", gu);
        } else {
            strlcpy(config.gravityUnit, gu, sizeof(config.gravityUnit));
            Log.notice("Settings update, [gravityUnit]:(%s) applied.\r\n", gu);
        }
    }

    // smoothFactor
    if(json["smoothFactor"].is<uint8_t>()) {
        if(json["smoothFactor"].as<int>() < 0 || json["smoothFactor"].as<int>() > 99) {
            Log.warning("Settings update error, [smoothFactor]:(%d) not valid.\r\n", json["smoothFactor"].as<uint8_t>());
        } else {
            config.smoothFactor = json["smoothFactor"];
            Log.notice("Settings update, [smoothFactor]:(%d) applied.\r\n", json["smoothFactor"].as<uint8_t>());
        }
    }

    // invertTFT
    if(json["invertTFT"].is<bool>()) {
        if(config.invertTFT != json["invertTFT"].as<bool>())
            http_server.lcd_reinit_rqd = true;
        config.invertTFT = json["invertTFT"];
        if(json["invertTFT"].as<bool>())
            Log.notice("Settings update, [invertTFT]:(True) applied.\r\n");
        else
            Log.notice("Settings update, [invertTFT]:(False) applied.\r\n");
    }

    // combineTilts
    if(json["combineTilts"].is<bool>()) {
        if(config.combineTilts != json["combineTilts"].as<bool>()) {
            config.combineTilts = json["combineTilts"];
            tilt_scanner.m_tilt_devices.clear();
            Log.notice("Settings update, [combineTilts]:(%s) applied. Tilt list cleared.\r\n",
                       config.combineTilts ? "True" : "False");
        }
    }

    // queueSnapshotIntervalSec - device-wide, so it lives here rather than with any one target
    if(json[QueueSettings::queueSnapshotIntervalSec].is<uint16_t>()) {
        uint16_t interval = json[QueueSettings::queueSnapshotIntervalSec].as<uint16_t>();
        if(interval < 60 || interval > 21600) {  // the 60 s floor protects flash from snapshot churn
            Log.warning("Settings update error, [queueSnapshotIntervalSec]:(%d) not valid.\r\n", interval);
            failCount++;
        } else {
            if(config.queueSnapshotIntervalSec != interval)
                http_server.queue_timer_restart_rqd = true;  // the running one-shot is re-armed by the main loop
            config.queueSnapshotIntervalSec = interval;
            Log.notice("Settings update, [queueSnapshotIntervalSec]:(%d) applied.\r\n", interval);
        }
    }

    // maxQueuedRecords
    if(json[QueueSettings::maxQueuedRecords].is<uint16_t>()) {
        uint16_t maxRecords = json[QueueSettings::maxQueuedRecords].as<uint16_t>();

        // The static ceiling is 3000 (384 KB), but the LittleFS partition is shared with the
        // web UI and rarely has that spare. Refuse what the flash cannot hold rather than
        // accepting it and letting the queue shed records it was told to keep.
        uint16_t ceiling = QUEUE_MAX_RECORDS_CEILING;
        const uint16_t supported = reading_queue.storageCapacityRecords();
        if(supported > 0 && supported < ceiling)
            ceiling = supported;

        if(maxRecords < 100 || maxRecords > ceiling) {
            Log.warning("Settings update error, [maxQueuedRecords]:(%d) outside 100..%d "
                        "(this filesystem supports %d).\r\n",
                        maxRecords, ceiling, supported);
            failCount++;
        } else {
            config.maxQueuedRecords = maxRecords;
            Log.notice("Settings update, [maxQueuedRecords]:(%d) applied.\r\n", maxRecords);
        }
    }

    // queueBatchSize
    if(json[QueueSettings::queueBatchSize].is<uint8_t>()) {
        uint8_t batchSize = json[QueueSettings::queueBatchSize].as<uint8_t>();
        if(batchSize < 1 || batchSize > 50) {  // 50 records is already a ~13 KB POST body
            Log.warning("Settings update error, [queueBatchSize]:(%d) not valid.\r\n", batchSize);
            failCount++;
        } else {
            config.queueBatchSize = batchSize;
            Log.notice("Settings update, [queueBatchSize]:(%d) applied.\r\n", batchSize);
        }
    }

    // senderStaleRebootSec
    if(json["senderStaleRebootSec"].is<uint16_t>()) {
        uint16_t staleSec = json["senderStaleRebootSec"].as<uint16_t>();
        if(staleSec < 60 || staleSec > 600) {
            Log.warning("Settings update error, [senderStaleRebootSec]:(%d) not valid.\r\n", staleSec);
            failCount++;
        } else {
            config.senderStaleRebootSec = staleSec;
            Log.notice("Settings update, [senderStaleRebootSec]:(%d) applied.\r\n", staleSec);
        }
    }

    // Optional booleans - the return value is deliberately ignored, as it is also false
    // when the key is simply absent from a partial controller settings update
    updateJsonSettingBool(json, QueueSettings::offlineQueueEnabled, config.offlineQueueEnabled);
    updateJsonSettingBool(json, "senderRecoveryEnabled", config.senderRecoveryEnabled);

    // Process everything we were passed
    if (failCount) {
        Log.error("Error: Invalid controller configuration.\r\n");
    } else {
        if (config.save()) {
            if (hostnamechanged) {
                hostnamechanged = false;
                wifi_cfg_set_var("mdns_name", config.mdnsID);
                http_server.name_reset_requested = true;
                Log.notice("Received new mDNSid, queued network reset.\r\n");
            }
        } else {
            Log.error("Error: Unable to save controller configuration data.\r\n");
            failCount++;
        }
    }
    return failCount == 0;
}

static bool processCalibrationSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSettingBool(json, CalibrationKeys::applyCalibration, config.applyCalibration))
        failCount++;

    if(!updateJsonSettingBool(json, CalibrationKeys::tempCorrect, config.tempCorrect))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid upstream configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save calibration configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processFermentrackSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    bool update_legacy = false;
    bool update_ft2 = false;

    /*
     * The FT2 push interval is applied outside the branch below, and its presence must never
     * be what selects a branch: the FT2 branch clears the device id and API key to force
     * re-registration, and changing how often readings are uploaded is no reason to do that.
     */
    if(!applyPushEvery(json, FermentrackSettings::fermentrackPushEvery, config.fermentrackPushEvery))
        failCount++;

    if (json[FermentrackSettings::legacyFermentrackPushEvery].is<uint16_t>()) {
        Log.info("Received legacy fermentrack settings.\r\n");
        update_legacy = true;

        if(!updateJsonSetting(json, FermentrackSettings::legacyFermentrackURL, config.legacyFermentrackURL, 256))
            failCount++;

        if(!updateJsonSetting(json, FermentrackSettings::legacyFermentrackPushEvery, config.legacyFermentrackPushEvery))
            failCount++;
        if(config.legacyFermentrackPushEvery < 30 || config.legacyFermentrackPushEvery > 43200) {
            Log.warning("Settings update error, [legacyFermentrackPushEvery]:(%d) not valid.\r\n", config.legacyFermentrackPushEvery);
            config.legacyFermentrackPushEvery = 30;
            failCount++;
        }
    } else if (json[FermentrackSettings::fermentrackHostname].is<const char*>()) {
        Log.info("Received FT2 settings.\r\n");
        update_ft2 = true;
        config.fermentrackDeviceID[0] = '\0';
        config.fermentrackAPIKey[0] = '\0';
        fermentrackRegistrationError = fermentrackRegErrorT::NOT_ATTEMPTED_REGISTRATION;

        if(!updateJsonSetting(json, FermentrackSettings::fermentrackHostname, config.fermentrackHostname, sizeof(config.fermentrackHostname)))
            failCount++;
        if(!updateJsonSetting(json, FermentrackSettings::fermentrackPort, config.fermentrackPort))
            failCount++;
        if(config.fermentrackPort < 10) {
            Log.warning("Settings update error, [fermentrackPort]:(%d) not valid.\r\n", config.fermentrackPort);
            config.fermentrackPort = 80;
            failCount++;
        }
        if(!updateJsonSetting(json, FermentrackSettings::fermentrackUsername, config.fermentrackUsername, sizeof(config.fermentrackUsername)))
            failCount++;
    }

    if(failCount > 0) {
        Log.error("Error: Invalid Fermentrack target configuration.\r\n");
    } else {
        if (!config.save()) {
            Log.error("Error: Unable to save Fermentrack target configuration data.\r\n");
            failCount++;
        } else {
            if(update_legacy)
                startSendNowTimer(sendNowLegacyFTTimer, "SendLegacyFT", sendNowLegacyFTCallback, 3);
            if(update_ft2)
                startSendNowTimer(sendNowFTTimer, "SendFT", sendNowFTCallback, 5);
        }
    }

    return failCount == 0;
}

static bool processGoogleSheetsSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    /*
     * Whether the credentials actually changed must be decided BEFORE they are overwritten.
     *
     * The immediate send below used to fire on every write to this endpoint, so changing
     * only the push interval queued one too - and because the previous interval's timer was
     * still counting down, that produced two uploads seconds apart followed by a gap.
     * A new URL or email is the only thing worth confirming straight away.
     */
    const bool urlChanged =
        json[GoogleSheetsSettings::scriptsURL].is<const char*>() &&
        strcmp(json[GoogleSheetsSettings::scriptsURL].as<const char*>(), config.scriptsURL) != 0;

    const bool emailChanged =
        json[GoogleSheetsSettings::scriptsEmail].is<const char*>() &&
        strcmp(json[GoogleSheetsSettings::scriptsEmail].as<const char*>(), config.scriptsEmail) != 0;

    const uint16_t previousPushEvery = config.gsheetsPushEvery;

    if(!updateJsonSetting(json, GoogleSheetsSettings::scriptsURL, config.scriptsURL, 256))
        failCount++;
    if(!updateJsonSetting(json, GoogleSheetsSettings::scriptsEmail, config.scriptsEmail, 256))
        failCount++;

    /*
     * Gated on the sender's own minimums rather than the 26/5 that used to be here, which
     * disagreed with them: a 6-character email passed this check and was then rejected by
     * send_to_google_v2(), queueing a send that could only mark the target DISABLED.
     */
    if((urlChanged || emailChanged) &&
       strlen(config.scriptsURL) >= GSCRIPTS_MIN_URL_LENGTH &&
       strlen(config.scriptsEmail) >= GSCRIPTS_MIN_EMAIL_LENGTH)
        startSendNowTimer(sendNowGSheetsTimer, "SendGSheets", sendNowGSheetsCallback, 5);

    /*
     * Per-colour sheet names are OPTIONAL in the payload.
     *
     * They are no longer edited here - a sheet name belongs to a Tilt, set per device on the
     * Tilts page, and device_config.sheetName() prefers that. These remain only as the
     * fallback for a Tilt that has no device config yet, so a client that does not send them
     * must not have its whole update rejected. Requiring them is what made a partial payload
     * to this endpoint fail.
     */
    uint8_t i = 0;
    for(const char* sheetKey : tiltColorSuffixes) {
        char full_key[30];
        snprintf(full_key, 30, "%s%s", GoogleSheetsSettings::gsheetsPrefix, sheetKey);

        if(json[full_key].is<const char*>()) {
            if(!updateJsonSetting(json, full_key, config.gsheets_config[i].name, 25))
                failCount++;
        }
        i++;
    }

    // Optional - absent means "leave the current mode alone", so the return value is ignored
    updateJsonSettingBool(json, GoogleSheetsSettings::gsheetsV2Enabled, config.gsheetsV2Enabled);

    if(!applyPushEvery(json, GoogleSheetsSettings::gsheetsPushEvery, config.gsheetsPushEvery))
        failCount++;

    /*
     * Re-arm against the new interval now.
     *
     * startTimer() is otherwise reached only at the end of a completed send, so a changed
     * interval did not take effect until one more upload had gone out on the OLD schedule -
     * the previous countdown simply kept running. backoffDelay() is applied for the same
     * reason the send path applies it: changing an interval must not reset a target that is
     * deliberately backing off from a failing endpoint.
     *
     * The new period is measured from the LAST UPLOAD, not from this request, so consecutive
     * rows really are the configured interval apart. Re-arming with the full period here
     * instead would add however long ago the last upload was: changing 10 -> 15 shortly after
     * an upload produced a 15-minute wait on top of that, so the first gap in the sheet was
     * longer than 15 minutes and only later ones were right.
     *
     * If the new interval has already elapsed - shortening 15 -> 5 twelve minutes in - the
     * send is due now, and goes out on a short delay rather than instantly so it cannot race
     * the config.save() below.
     */
    if(config.gsheetsPushEvery != previousPushEvery) {
        const uint32_t period = data_sender.backoffDelay(TARGET_GOOGLE_SHEETS,
                                                         config.gsheetsPushEvery);
        // sh_millis()/1000, matching what setTargetStatus() stores. NOT uptimeSeconds(), which
        // returns the 0..59 seconds component and made this silently fall back to the full
        // period whenever the two readings straddled a minute boundary.
        const uint32_t lastAttempt = data_sender.targetStatus[TARGET_GOOGLE_SHEETS].lastAttemptTime;
        const uint32_t now = sh_millis() / 1000;

        uint32_t remaining = period;
        if(lastAttempt > 0 && now >= lastAttempt) {
            const uint32_t elapsed = now - lastAttempt;
            remaining = (elapsed >= period) ? PUSH_INTERVAL_DUE_NOW_SEC : (period - elapsed);
        }

        data_sender.startTimer(data_sender.gSheetsTimer, remaining);
        Log.notice("Google Sheets interval %u -> %us; next upload in %us.\r\n",
                   (unsigned)previousPushEvery, (unsigned)config.gsheetsPushEvery,
                   (unsigned)remaining);
    }

    if(failCount > 0) {
        Log.error("Error: Invalid Google Sheets configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Google Sheets configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processBrewersFriendSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, BrewersFriendSettings::brewersFriendKey, config.brewersFriendKey, 64))
        failCount++;
    if(strlen(config.brewersFriendKey) > BREWERS_FRIEND_MIN_KEY_LENGTH)
        startSendNowTimer(sendNowBrewersFriendTimer, "SendBF", sendNowBrewersFriendCallback, 5);

    if(!applyPushEvery(json, BrewersFriendSettings::brewersFriendPushEvery, config.brewersFriendPushEvery))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid Brewer's Friend configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Brewer's Friend configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processBrewfatherSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, BrewfatherSettings::brewfatherKey, config.brewfatherKey, 64))
        failCount++;
    if(strlen(config.brewfatherKey) > BREWFATHER_MIN_KEY_LENGTH)
        startSendNowTimer(sendNowBrewfatherTimer, "SendBrewfather", sendNowBrewfatherCallback, 5);

    if(!applyPushEvery(json, BrewfatherSettings::brewfatherPushEvery, config.brewfatherPushEvery))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid Brewfather configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Brewfather configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processUserTargetSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, UserTargetSettings::userTargetURL, config.userTargetURL, 128))
        failCount++;
    if(strlen(config.userTargetURL) > USER_TARGET_MIN_URL_LENGTH)
        startSendNowTimer(sendNowUserTargetTimer, "SendUserTarget", sendNowUserTargetCallback, 5);

    if(!applyPushEvery(json, UserTargetSettings::userTargetPushEvery, config.userTargetPushEvery))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid user target configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save user target configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processGrainfatherSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    uint8_t i = 0;
    for(const char* sheetKey : tiltColorSuffixes) {
        char full_key[35];
        snprintf(full_key, 35, "%s%s", GrainfatherSettings::grainfatherURLPrefix, sheetKey);

        if(!updateJsonSetting(json, full_key, config.grainfatherURL[i].link, 64))
            failCount++;
        i++;
    }

    if(!applyPushEvery(json, GrainfatherSettings::grainfatherPushEvery, config.grainfatherPushEvery))
        failCount++;

    startSendNowTimer(sendNowGrainfatherTimer, "SendGrainfather", sendNowGrainfatherCallback, 5);

    if(failCount > 0) {
        Log.error("Error: Invalid Grainfather configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Grainfather configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processBrewstatusSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, BrewstatusSettings::brewstatusURL, config.brewstatusURL, 256))
        failCount++;
    if(strlen(config.brewstatusURL) > 11)
        startSendNowTimer(sendNowBrewStatusTimer, "SendBrewStatus", sendNowBrewStatusCallback, 5);

    if(!updateJsonSetting(json, BrewstatusSettings::brewstatusPushEvery, config.brewstatusPushEvery))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid Brewstatus configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Brewstatus configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processTaplistioSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, TaplistioSettings::taplistioURL, config.taplistioURL, 256))
        failCount++;
    if(strlen(config.taplistioURL) > 11)
        startSendNowTimer(sendNowTaplistioTimer, "SendTaplistio", sendNowTaplistioCallback, 5);

    if(!updateJsonSetting(json, TaplistioSettings::taplistioPushEvery, config.taplistioPushEvery))
        failCount++;

    if(failCount > 0) {
        Log.error("Error: Invalid Taplist.io configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save Taplist.io configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processMqttSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, MQTTSettings::mqttBrokerHost, config.mqttBrokerHost, sizeof(config.mqttBrokerHost)))
        failCount++;

    if(!updateJsonSetting(json, MQTTSettings::mqttBrokerPort, config.mqttBrokerPort))
        failCount++;

    if(!updateJsonSetting(json, MQTTSettings::mqttPushEvery, config.mqttPushEvery))
        failCount++;

    if(!updateJsonSetting(json, MQTTSettings::mqttUsername, config.mqttUsername, sizeof(config.mqttUsername)))
        failCount++;

    if(!updateJsonSetting(json, MQTTSettings::mqttPassword, config.mqttPassword, sizeof(config.mqttPassword)))
        failCount++;

    if(!updateJsonSetting(json, MQTTSettings::mqttTopic, config.mqttTopic, sizeof(config.mqttTopic)))
        failCount++;

    http_server.mqtt_init_rqd = true;
    startSendNowTimer(sendNowMqttTimer, "SendMQTT", sendNowMqttCallback, 5);

    if(failCount > 0) {
        Log.error("Error: Invalid MQTT configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save MQTT configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}

static bool processInfluxdbSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    uint8_t failCount = 0;

    if(!updateJsonSetting(json, InfluxDBSettings::influxdbURL, config.influxdbURL, sizeof(config.influxdbURL)))
        failCount++;
    if(!updateJsonSetting(json, InfluxDBSettings::influxdbToken, config.influxdbToken, sizeof(config.influxdbToken)))
        failCount++;
    if(!updateJsonSetting(json, InfluxDBSettings::influxdbOrg, config.influxdbOrg, sizeof(config.influxdbOrg)))
        failCount++;
    if(!updateJsonSetting(json, InfluxDBSettings::influxdbBucket, config.influxdbBucket, sizeof(config.influxdbBucket)))
        failCount++;
    if(!updateJsonSetting(json, InfluxDBSettings::influxdbPushEvery, config.influxdbPushEvery))
        failCount++;

    if(strlen(config.influxdbURL) > INFLUXDB_MIN_URL_LENGTH)
        startSendNowTimer(sendNowInfluxdbTimer, "SendInfluxDB", sendNowInfluxdbCallback, 5);

    if(failCount > 0) {
        Log.error("Error: Invalid InfluxDB configuration.\r\n");
    } else if (!config.save()) {
        Log.error("Error: Unable to save InfluxDB configuration data.\r\n");
        failCount++;
    }

    return failCount == 0;
}


/**
 * @brief Unified target settings dispatcher
 *
 * Detects which target's keys are present in the JSON and delegates
 * to the appropriate process*Settings helper.
 */
static bool processTargetSettings(const JsonDocument& json, bool triggerUpstreamUpdate) {
    // Fermentrack — legacy key, FT2 connection details, or the FT2 push interval on its own
    if (json[FermentrackSettings::legacyFermentrackPushEvery].is<uint16_t>() ||
        json[FermentrackSettings::fermentrackHostname].is<const char*>() ||
        json[FermentrackSettings::fermentrackPushEvery].is<uint16_t>())
        return processFermentrackSettings(json, triggerUpstreamUpdate);

    if (json[GoogleSheetsSettings::scriptsURL].is<const char*>())
        return processGoogleSheetsSettings(json, triggerUpstreamUpdate);

    if (json[BrewersFriendSettings::brewersFriendKey].is<const char*>())
        return processBrewersFriendSettings(json, triggerUpstreamUpdate);

    if (json[BrewfatherSettings::brewfatherKey].is<const char*>())
        return processBrewfatherSettings(json, triggerUpstreamUpdate);

    if (json[UserTargetSettings::userTargetURL].is<const char*>())
        return processUserTargetSettings(json, triggerUpstreamUpdate);

    if (json["grainfatherURL_red"].is<const char*>())
        return processGrainfatherSettings(json, triggerUpstreamUpdate);

    if (json[BrewstatusSettings::brewstatusURL].is<const char*>())
        return processBrewstatusSettings(json, triggerUpstreamUpdate);

    if (json[TaplistioSettings::taplistioURL].is<const char*>())
        return processTaplistioSettings(json, triggerUpstreamUpdate);

    if (json[MQTTSettings::mqttBrokerHost].is<const char*>())
        return processMqttSettings(json, triggerUpstreamUpdate);

    if (json[InfluxDBSettings::influxdbURL].is<const char*>())
        return processInfluxdbSettings(json, triggerUpstreamUpdate);

    Log.warning("No recognized target keys in JSON payload.\r\n");
    return false;
}


//=============================================================================
// HTTP Handler Wrappers (bridge existing functions to httpd_req_handler_t)
//=============================================================================

// Type for GET JSON handlers
typedef void (*json_get_handler_t)(JsonDocument &);

// Type for PUT/POST JSON handlers
typedef bool (*json_put_handler_t)(const JsonDocument &, bool);

/**
 * @brief Generic wrapper for GET JSON endpoints
 *
 * Calls the provided handler function to populate a JsonDocument,
 * then sends it as a JSON response.
 */
static esp_err_t json_get_wrapper(httpd_req_t *req, json_get_handler_t handler) {
    JsonDocument doc;
    handler(doc);
    return idf_json_send_response(req, doc);
}

/**
 * @brief Generic wrapper for PUT/POST JSON endpoints
 *
 * Parses the request body as JSON, calls the handler, and returns status.
 */
static esp_err_t json_put_wrapper(httpd_req_t *req, json_put_handler_t handler) {
    JsonDocument doc;

    if (idf_json_parse_body(req, doc) != ESP_OK) {
        return ESP_OK;  // Error response already sent
    }

    /*
     * A settings update is applied ATOMICALLY: either every field lands, or none does.
     *
     * The process*Settings() handlers write straight into `config` as they go and only
     * accumulate a failure count, saving to flash at the end if it is zero. That left a
     * rejected update having already mutated the RUNNING config while flash kept the old
     * values - so the device would report the update as failed, keep operating on the new
     * values anyway, and silently revert on the next reboot. A partial payload to
     * /api/settings/targets/ was enough to trigger it, because those handlers count an
     * absent key as a failure.
     *
     * Snapshotting and rolling back is done here rather than in each of the eleven
     * handlers so no handler can be missed, and so it keeps holding for handlers added
     * later. Config is plain data plus fixed-size buffers, so the implicit copy is a
     * faithful snapshot; it goes on the heap because it is several KB and this runs on the
     * HTTP server task.
     */
    Config *previous = new (std::nothrow) Config(config);
    const fermentrackRegErrorT previousRegError = fermentrackRegistrationError;

    const bool success = handler(doc, true);

    if (!success) {
        if (previous != nullptr) {
            config = *previous;
            fermentrackRegistrationError = previousRegError;
            Log.warning("Settings update rejected; running configuration rolled back.\r\n");
        } else {
            // Could not snapshot, so the partial update stands. Say so - the alternative is
            // a device quietly running values it just told the caller it refused.
            Log.error("Settings update rejected but could not be rolled back (out of memory). "
                      "Reboot to reload the saved configuration.\r\n");
        }
    }

    delete previous;
    return idf_json_send_status(req, success);
}

// Macro to create httpd handler from GET JSON function
#define MAKE_GET_HANDLER(name, func) \
    static esp_err_t name(httpd_req_t *req) { \
        return json_get_wrapper(req, func); \
    }

// Macro to create httpd handler from PUT JSON function
#define MAKE_PUT_HANDLER(name, func) \
    static esp_err_t name(httpd_req_t *req) { \
        return json_put_wrapper(req, func); \
    }

// Generate GET handlers
MAKE_GET_HANDLER(handle_api_json, http_json)
MAKE_GET_HANDLER(handle_api_settings_json, settings_json)
MAKE_GET_HANDLER(handle_api_version, this_version)
MAKE_GET_HANDLER(handle_api_uptime, uptime_json)
MAKE_GET_HANDLER(handle_api_heap, heap_json)
MAKE_GET_HANDLER(handle_api_resetreason, reset_reason_json)
MAKE_GET_HANDLER(handle_api_errors, errors_json)
MAKE_GET_HANDLER(handle_api_sender, sender_json)
MAKE_GET_HANDLER(handle_api_devices, devices_json)
MAKE_GET_HANDLER(handle_api_queue, queue_json)

// Generate PUT handlers
MAKE_PUT_HANDLER(handle_settings_controller, processTiltBridgeSettingsJson)
MAKE_PUT_HANDLER(handle_settings_calibration, processCalibrationSettings)
MAKE_PUT_HANDLER(handle_settings_targets, processTargetSettings)

// Calibration POST handlers
MAKE_PUT_HANDLER(handle_calibration_datapoint, processCalibrationDataPoint)
MAKE_PUT_HANDLER(handle_calibration_coefficients, processCalibrationCoefficients)
MAKE_PUT_HANDLER(handle_calibration_delete, processCalibrationDataDelete)

// Device configuration upsert. Creates the entry when it does not exist, which is how a
// user attaches settings to a newly detected physical Tilt.
static esp_err_t handle_devices_put(httpd_req_t *req) {
    JsonDocument doc;

    if (idf_json_parse_body(req, doc) != ESP_OK) {
        return ESP_OK;  // Error response already sent
    }

    const char *err = nullptr;
    if (!device_config.upsert_from_json(doc, &err)) {
        Log.warning("Device config update rejected: %s\r\n", err ? err : "unknown");
        return idf_json_send_error(req, 400, err ? err : "Invalid device configuration");
    }

    return idf_json_send_status(req, true);
}

// Removing a device entry reverts that Tilt to the shared colour configuration.
static esp_err_t handle_devices_delete(httpd_req_t *req) {
    JsonDocument doc;

    if (idf_json_parse_body(req, doc) != ESP_OK) {
        return ESP_OK;
    }

    const char *rawId = doc["deviceId"].as<const char*>();
    if (!isValidDeviceId(rawId)) {
        return idf_json_send_error(req, 400, "Invalid or missing deviceId");
    }

    char id[DEVICE_ID_LEN];
    canonicalizeDeviceId(rawId, id, sizeof(id));

    if (!device_config.remove(id)) {
        return idf_json_send_error(req, 404, "No configuration exists for that device");
    }

    if (!device_config.save()) {
        return idf_json_send_error(req, 500, "Unable to save device configuration");
    }

    Log.notice("Device configuration removed for %s; reverting to colour settings.\r\n", id);
    return idf_json_send_status(req, true);
}

// Queue actions. Clearing is destructive, so it requires an explicit confirm flag on the
// wire as well as a confirmation step in the UI.
static esp_err_t handle_queue_actions(httpd_req_t *req) {
    JsonDocument doc;

    if (idf_json_parse_body(req, doc) != ESP_OK) {
        return ESP_OK;
    }

    const char *action = doc["action"];
    if (!action) {
        return idf_json_send_error(req, 400, "Missing 'action' field");
    }

    if (strcmp(action, "sendBacklogNow") == 0) {
        if (!config.gsheetsV2Enabled) {
            return idf_json_send_error(req, 400,
                "Enhanced Google Sheets mode is off; the backlog has no configured destination");
        }
        data_sender.send_backlog_now = true;
        Log.notice("Backlog upload requested via API.\r\n");

    } else if (strcmp(action, "clearQueue") == 0) {
        if (!doc["confirm"].is<bool>() || !doc["confirm"].as<bool>()) {
            return idf_json_send_error(req, 400,
                "Clearing the queue permanently discards queued readings and requires confirm:true");
        }
        const size_t discarded = reading_queue.pendingCount();
        reading_queue.clear();
        Log.warning("Queue cleared via API; %u pending readings discarded.\r\n", (unsigned)discarded);

    } else {
        return idf_json_send_error(req, 400, "Unknown action");
    }

    return idf_json_send_status(req, true);
}

// Action handler — dispatches based on "action" field in JSON body
static esp_err_t handle_action(httpd_req_t *req) {
    JsonDocument doc;

    if (idf_json_parse_body(req, doc) != ESP_OK) {
        return ESP_OK;  // Error response already sent
    }

    const char *action = doc["action"];
    if (!action) {
        return idf_json_send_error(req, 400, "Missing 'action' field");
    }

    if (strcmp(action, "resetWifi") == 0) {
        http_server.wifi_reset_requested = true;
#ifdef TB_DEBUG_FREEZE
    } else if (strcmp(action, "debugFreezeSender") == 0) {
        // Acceptance-test hook (T6): stop refreshing the sender heartbeat so the health
        // monitor sees the outbound loop as wedged. Compiled in only with -D TB_DEBUG_FREEZE=1.
        sender_health.debugFreeze = true;
        Log.warning("DEBUG: sender heartbeat frozen by request.\r\n");
#endif
    } else if (strcmp(action, "resetDevice") == 0) {
        http_server.factoryreset_requested = true;
    } else if (strcmp(action, "restartDevice") == 0) {
        http_server.restart_requested = true;
    } else {
        return idf_json_send_error(req, 400, "Unknown action");
    }

    return idf_json_send_status(req, true);
}


//=============================================================================
// httpServer class implementation
//=============================================================================

void httpServer::registerJsonGetHandlers() {
    struct {
        const char *uri;
        esp_err_t (*handler)(httpd_req_t *);
    } get_endpoints[] = {
        {"/api/json/", handle_api_json},
        {"/api/settings/json/", handle_api_settings_json},
        {"/api/version/", handle_api_version},
        {"/api/uptime/", handle_api_uptime},
        {"/api/heap/", handle_api_heap},
        {"/api/resetreason/", handle_api_resetreason},
        {"/api/errors/", handle_api_errors},
        {"/api/sender/", handle_api_sender},
        {"/api/devices/", handle_api_devices},
        {"/api/queue/", handle_api_queue},
    };

    for (const auto& endpoint : get_endpoints) {
        httpd_uri_t uri_config = {
            .uri = endpoint.uri,
            .method = HTTP_GET,
            .handler = endpoint.handler,
            .user_ctx = NULL
        };
        idf_httpd_register_uri(&uri_config);
    }

    ESP_LOGI(TAG, "Registered JSON GET handlers");
}

void httpServer::registerJsonPutHandlers() {
    struct {
        const char *uri;
        esp_err_t (*handler)(httpd_req_t *);
    } put_endpoints[] = {
        {"/api/settings/controller/",  handle_settings_controller},
        {"/api/settings/calibration/", handle_settings_calibration},
        {"/api/settings/targets/",     handle_settings_targets},
    };

    for (const auto& endpoint : put_endpoints) {
        httpd_uri_t uri_config = {
            .uri = endpoint.uri,
            .method = HTTP_PUT,
            .handler = endpoint.handler,
            .user_ctx = NULL
        };
        idf_httpd_register_uri(&uri_config);
    }

    ESP_LOGI(TAG, "Registered JSON PUT handlers");
}

void httpServer::registerCalibrationHandlers() {
    // POST: Add calibration data point
    httpd_uri_t datapoint_uri = {
        .uri = "/api/calibration/datapoint/",
        .method = HTTP_POST,
        .handler = handle_calibration_datapoint,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&datapoint_uri);

    // PUT: Update calibration coefficients
    httpd_uri_t coefficients_uri = {
        .uri = "/api/calibration/coefficients/",
        .method = HTTP_PUT,
        .handler = handle_calibration_coefficients,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&coefficients_uri);

    // POST: Delete calibration data point
    httpd_uri_t delete_uri = {
        .uri = "/api/calibration/datapoint/delete/",
        .method = HTTP_POST,
        .handler = handle_calibration_delete,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&delete_uri);

    ESP_LOGI(TAG, "Registered calibration handlers");
}

void httpServer::registerActionHandlers() {
    httpd_uri_t uri_config = {
        .uri = "/api/actions/",
        .method = HTTP_POST,
        .handler = handle_action,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&uri_config);

    ESP_LOGI(TAG, "Registered action handlers");
}

void httpServer::registerDeviceHandlers() {
    // PUT upserts a single device; the GET half is registered with the other JSON GETs.
    httpd_uri_t put_uri = {
        .uri = "/api/devices/",
        .method = HTTP_PUT,
        .handler = handle_devices_put,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&put_uri);

    // POST rather than DELETE, matching the existing calibration delete endpoint.
    httpd_uri_t delete_uri = {
        .uri = "/api/devices/delete/",
        .method = HTTP_POST,
        .handler = handle_devices_delete,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&delete_uri);

    httpd_uri_t queue_actions_uri = {
        .uri = "/api/queue/actions/",
        .method = HTTP_POST,
        .handler = handle_queue_actions,
        .user_ctx = NULL
    };
    idf_httpd_register_uri(&queue_actions_uri);

    ESP_LOGI(TAG, "Registered device configuration and queue handlers");
}

void httpServer::init() {
    // Start the HTTP server with worker pool
    // esp_err_t ret = idf_httpd_start();
    // if (ret != ESP_OK) {
    //     Log.error("Failed to start HTTP server: %s\r\n", esp_err_to_name(ret));
    //     return;
    // }

    // Register static file and SPA handlers first
    idf_static_register_handlers();
    idf_static_register_spa_routes();

    // Register API handlers
    registerJsonGetHandlers();
    registerJsonPutHandlers();
    registerCalibrationHandlers();
    registerActionHandlers();
    registerDeviceHandlers();

    // Register catch-all for static files LAST (so specific routes take precedence)
    idf_static_register_catchall();

    Log.notice("HTTP server started. Open: http://%s.local/ to view application.\r\n", config.mdnsID);
}

void httpServer::stop() {
    idf_httpd_stop();
    Log.notice("HTTP server stopped.\r\n");
}
