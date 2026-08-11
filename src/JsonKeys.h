constexpr const char* tiltColorSuffixes[] = {
    "_red",       // 0 = TILT_COLOR_RED
    "_green",     // 1 = TILT_COLOR_GREEN
    "_black",     // 2 = TILT_COLOR_BLACK
    "_purple",    // 3 = TILT_COLOR_PURPLE
    "_orange",    // 4 = TILT_COLOR_ORANGE
    "_blue",      // 5 = TILT_COLOR_BLUE
    "_yellow",    // 6 = TILT_COLOR_YELLOW
    "_pink",      // 7 = TILT_COLOR_PINK
};



namespace CalibrationKeys {
constexpr auto applyCalibration = "applyCalibration";
constexpr auto tempCorrect = "tempCorrect";
}; // namespace CalibrationKeys

namespace GeneralSettings {
constexpr auto gravityUnit = "gravityUnit";
}; // namespace GeneralSettings


namespace FermentrackSettings {
// Legacy Fermentrack Keys
constexpr auto legacyFermentrackURL = "legacyFermentrackURL";
constexpr auto legacyFermentrackPushEvery = "legacyFermentrackPushEvery";

// Fermentrack 2 Keys
constexpr auto fermentrackHostname = "fermentrackHostname";
constexpr auto fermentrackPort = "fermentrackPort";
constexpr auto fermentrackUsername = "fermentrackUsername";
constexpr auto fermentrackDeviceID = "fermentrackDeviceID";
constexpr auto fermentrackAPIKey = "fermentrackAPIKey";
constexpr auto fermentrackPushEvery = "fermentrackPushEvery";
}; // namespace FermentrackSettings

namespace GoogleSheetsSettings {
constexpr auto scriptsURL = "scriptsURL";
constexpr auto scriptsEmail = "scriptsEmail";
constexpr auto gsheetsPrefix = "sheetName";
constexpr auto gsheetsV2Enabled = "gsheetsV2Enabled";
constexpr auto gsheetsPushEvery = "gsheetsPushEvery";
}

namespace QueueSettings {
constexpr auto offlineQueueEnabled = "offlineQueueEnabled";
constexpr auto queueSnapshotIntervalSec = "queueSnapshotIntervalSec";
constexpr auto maxQueuedRecords = "maxQueuedRecords";
constexpr auto queueBatchSize = "queueBatchSize";
}


namespace BrewersFriendSettings {
constexpr auto brewersFriendKey = "brewersFriendKey";
constexpr auto brewersFriendPushEvery = "brewersFriendPushEvery";
}

namespace BrewfatherSettings {
constexpr auto brewfatherKey = "brewfatherKey";
constexpr auto brewfatherPushEvery = "brewfatherPushEvery";
}

namespace UserTargetSettings {
constexpr auto userTargetURL = "userTargetURL";
constexpr auto userTargetPushEvery = "userTargetPushEvery";
}

namespace GrainfatherSettings {
constexpr auto grainfatherURLPrefix = "grainfatherURL";
constexpr auto grainfatherPushEvery = "grainfatherPushEvery";
}

namespace BrewstatusSettings {
constexpr auto brewstatusURL = "brewstatusURL";
constexpr auto brewstatusPushEvery = "brewstatusPushEvery";
}

namespace TaplistioSettings {
constexpr auto taplistioURL = "taplistioURL";
constexpr auto taplistioPushEvery = "taplistioPushEvery";
}

namespace MQTTSettings {
constexpr auto mqttBrokerHost = "mqttBrokerHost";
constexpr auto mqttBrokerPort = "mqttBrokerPort";
constexpr auto mqttPushEvery = "mqttPushEvery";
constexpr auto mqttUsername = "mqttUsername";
constexpr auto mqttPassword = "mqttPassword";
constexpr auto mqttTopic = "mqttTopic";
}

namespace InfluxDBSettings {
constexpr auto influxdbURL = "influxdbURL";
constexpr auto influxdbToken = "influxdbToken";
constexpr auto influxdbOrg = "influxdbOrg";
constexpr auto influxdbBucket = "influxdbBucket";
constexpr auto influxdbPushEvery = "influxdbPushEvery";
}

