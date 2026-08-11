#ifndef TILTBRIDGE_SENDDATA_H
#define TILTBRIDGE_SENDDATA_H

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <ArduinoJson.h>

#include "mqtt_client.h"
#include "tilt/tiltHydrometer.h"
#include "targets/send_json_str.h"

// Only pointers to it are passed across this interface, so the queue's record layout does
// not need to be visible to everything that includes this header.
struct QueuedReading;

// Send target identifiers for error tracking
enum SendTargetID : uint8_t {
    TARGET_LEGACY_FERMENTRACK = 0,
    TARGET_FERMENTRACK,
    TARGET_BREWERS_FRIEND,
    TARGET_BREWFATHER,
    TARGET_USER_TARGET,
    TARGET_GRAINFATHER,
    TARGET_BREW_STATUS,
    TARGET_TAPLISTIO,
    TARGET_GOOGLE_SHEETS,
    TARGET_MQTT,
    TARGET_INFLUXDB,
    TARGET_COUNT
};

// Error codes for send target status (interpreted by UI)
enum SendError : uint8_t {
    SEND_OK = 0,                        // Last send succeeded
    SEND_ERR_CONNECTION_FAILED = 1,     // TCP/DNS/TLS connection failure
    SEND_ERR_AUTH_FAILED = 2,           // HTTP 401/403, invalid credentials
    SEND_ERR_NOT_FOUND = 3,            // HTTP 404
    SEND_ERR_SERVER_ERROR = 4,         // HTTP 5xx
    SEND_ERR_BAD_REQUEST = 5,          // HTTP 400, malformed request or missing config
    SEND_ERR_RATE_LIMITED = 6,         // HTTP 429
    SEND_ERR_OTHER = 7,                // Other/unknown error
    SEND_ERR_MQTT_DISCONNECTED = 8,    // Not connected to MQTT broker
    // Fermentrack 2 specific errors
    SEND_ERR_FT2_INVALID_USER_OR_KEY = 9,   // User not found / invalid or missing API key / missing username
    SEND_ERR_FT2_MALFORMED_REG = 10,        // Missing GUID / hardware type / firmware version / device ID
    SEND_ERR_FT2_REG_INVALID = 11,          // Registration invalid (device ID/API key rejected)
    SEND_ERR_FT2_NO_BREWHOUSE = 12,         // User has no brewhouse
};

// Tracks the most recent send result for each target
struct SendTargetStatus {
    SendError lastError = SEND_OK;      // Most recent error code
    uint32_t lastAttemptTime = 0;       // uptime seconds when last attempted
    // Per-target, because a shared counter is reset by any healthy target and so hides a
    // target that is failing every single cycle. Also drives the retry backoff below.
    uint16_t consecutiveFailures = 0;
};

// After this many consecutive failures a target's retry interval starts doubling, so a dead
// endpoint stops claiming the sender on its normal schedule.
#define SEND_BACKOFF_AFTER_FAILURES 5
#define SEND_BACKOFF_MAX_SECONDS (30 * 60)

// Stable machine-readable names for each target, in SendTargetID order. Consumed by both
// the /api/errors/ and /api/sender/ endpoints, so the two always agree.
extern const char* const sendTargetNames[TARGET_COUNT];

// Push intervals used to live here as compile-time constants. They are now per-target settings
// on Config (config.gsheetsPushEvery and friends) so they can be changed from the web UI, which
// is also what makes them visibly distinct from config.queueSnapshotIntervalSec - how often the
// offline queue is written to flash, a device-wide setting that no push interval affects.
//
// The old values survive as the defaults in jsonconfig.h; nothing changes for a device that
// never touches the new fields.

// Per-target HTTP time budgets, handed to SenderLock so the health monitor knows how long a
// given target is legitimately allowed to hold the sender. These must track the timeoutMs
// actually used by each sender's HttpRequestOptions.
#define HTTP_TIMEOUT_DEFAULT_MS 6000    // HttpRequestOptions default (send_json_str.h)
#define HTTP_TIMEOUT_GSHEETS_MS 10000   // Google Scripts can be slow
#define HTTP_TIMEOUT_FERMENTRACK_MS (3 * HTTP_TIMEOUT_DEFAULT_MS)  // register + status + messages

#define BREWFATHER_MIN_KEY_LENGTH 5
#define BREWERS_FRIEND_MIN_KEY_LENGTH 12
#define BF_SIZE 192
#define GF_SIZE 256
#define FERMENTRACK_MIN_URL_LENGTH 9
#define BREWSTATUS_MIN_URL_LENGTH 12
#define GSCRIPTS_MIN_URL_LENGTH 24
#define GSCRIPTS_MIN_EMAIL_LENGTH 7
#define GSHEETS_JSON 512
#define INFLUXDB_MIN_URL_LENGTH 12

// This is me being lazy and simplifying the reuse of code. The formats for Brewer's
// Friend and Brewfather are basically the same so I'm combining them together
// in one function. I'm being even lazier by adding a user defined "send target"
// (user specified URL) to the same code block.
#define BF_MEANS_BREWFATHER 1
#define BF_MEANS_BREWERS_FRIEND 2
#define BF_MEANS_USER_TARGET 3

class dataSendHandler
{
public:
    dataSendHandler();
    void init();
    void init_mqtt();
    void process();

    bool send_to_google();
    bool send_to_legacy_fermentrack();
    bool send_to_fermentrack();
    bool send_to_brewstatus();
    bool send_to_taplistio();
    bool send_to_mqtt();
    bool send_to_bf_and_bf(uint8_t which_bf); // Handler for both Brewer's Friend and Brewfather
    bool send_to_grainfather();
    bool send_to_bf_and_bf();
    bool send_to_influxdb();

    // Enhanced batched Google Sheets protocol, drained from the persistent queue.
    bool send_to_google_v2();

    // Persists one record per enabled Tilt - but ONLY while the live sender is failing or a
    // backlog is waiting. In steady state readings are sent and dropped without ever
    // reaching flash, so this is the outage fallback rather than the normal data path.
    void take_queue_snapshot();

    // Build records from the scanner's current values. Touches neither flash nor the RSSI
    // windows, so both the live sender and the persistence path can use it.
    uint16_t collectCurrentReadings(QueuedReading *out, uint16_t maxRecords);
    void resetCollectedRssiIntervals(const QueuedReading *batch, uint16_t count);

    // True when readings must be written to flash rather than simply sent and dropped.
    bool queuePersistenceNeeded() const;

    // Throttles the "seen but not configured" warning so it cannot flood the log.
    uint32_t m_lastUnconfiguredWarnMs = 0;

    // Error tracking
    SendTargetStatus targetStatus[TARGET_COUNT];
    void setTargetStatus(SendTargetID target, SendError error);
    static SendError httpCodeToSendError(int16_t httpCode);

    /**
     * @brief Effective retry delay for a target, applying exponential backoff once it has
     *        failed repeatedly. Returns baseSeconds while the target is healthy.
     */
    uint32_t backoffDelay(SendTargetID target, uint32_t baseSeconds) const;

    // Send Timers (FreeRTOS software timers)
    TimerHandle_t legacyFermentrackTimer;
    TimerHandle_t fermentrackTimer;
    TimerHandle_t brewersFriendTimer;
    TimerHandle_t brewfatherTimer;
    TimerHandle_t userTargetTimer;
    TimerHandle_t grainfatherTimer;
    TimerHandle_t brewStatusTimer;
    TimerHandle_t taplistioTimer;
    TimerHandle_t gSheetsTimer;
    TimerHandle_t mqttTimer;
    TimerHandle_t influxdbTimer;
    TimerHandle_t queueSnapshotTimer;

    // Timer management methods
    void createTimers();
    void startTimer(TimerHandle_t timer, uint32_t periodSeconds);

    // Send Semaphores
    bool send_legacy_fermentrack = false;
    bool send_fermentrack = false;
    bool send_brewersFriend = false;
    bool send_brewfather = false;
    bool send_userTarget = false;
    bool send_grainfather = false;
    bool send_brewStatus = false;
    bool send_taplistio = false;
    bool send_gSheets = false;
    bool send_mqtt = false;
    bool send_influxdb = false;
    bool snapshot_due = false;
    bool send_backlog_now = false;      // set by /api/queue/actions/

    // Reported through /api/queue/ as uploadStatus
    enum class QueueUploadState : uint8_t { IDLE, SENDING, RETRYING, DISABLED };
    QueueUploadState queueUploadState = QueueUploadState::IDLE;
    uint32_t lastQueueUploadSuccessMs = 0;

private:
    // The former `bool send_lock` lived here. It has been replaced by the mutex owned by
    // SenderHealthMonitor (sender_health.h), claimed through the scoped SenderLock guard, so
    // that no send path can leave the sender permanently locked and so a wedged request is
    // visible to the health monitor.

    // MQTT Stuff
    esp_mqtt_client_handle_t mqtt_client = nullptr;
    bool mqtt_alreadyinit = false;
    bool mqtt_connected = false;

    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    void connect_mqtt();
    bool publish_to_mqtt(const char* topic, JsonDocument& payload, bool retain);


    void prepare_temperature_payload(tiltHydrometer *th, const char* tilt_topic);
    void prepare_gravity_payload(tiltHydrometer *th, const char* tilt_topic);
    void prepare_battery_payload(tiltHydrometer *th, const char* tilt_topic);
    void prepare_general_payload(tiltHydrometer *th, const char* tilt_topic);
    void enrich_announcement(const char* topic, const char* tilt_color, JsonDocument& payload);

};


extern dataSendHandler data_sender;

#endif //TILTBRIDGE_SENDDATA_H
