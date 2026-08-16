/*
 * Enhanced (queued, batched) Google Sheets protocol - schemaVersion 2.
 *
 * Deliberately isolated from the legacy send_to_google() path in sendData.cpp, which is
 * left untouched and remains the default. This path is selected only when the user sets
 * config.gsheetsV2Enabled, and it requires an Apps Script that implements the contract in
 * docs/phase1/APPS_SCRIPT_PROTOCOL.md.
 *
 * Delivery semantics are at-least-once with server-side duplicate suppression: a record
 * is removed from the queue only when the server echoes its id in acceptedRecordIds.
 */

#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <thorlog.h>
#include <ArduinoJson.h>

#include "../sendData.h"
#include "../sender_health.h"
#include "../jsonconfig.h"
#include "../time_sync.h"
#include "../queue/reading_queue.h"
#include "../tilt/tiltHydrometer.h"
#include "../tilt/tiltScanner.h"
#include "send_json_str.h"

// Apps Script answers /exec with a 302 to script.googleusercontent.com, so every upload
// costs TWO TLS handshakes plus the script's own execution. A desktop client needs ~14 s
// for that round trip; the ESP32's handshakes are far slower, and 15 s then 30 s both
// expired mid-flight (ESP_ERR_HTTP_EAGAIN). 60 s is sized for the whole two-handshake
// exchange. dataSendHandler::process() refreshes the sender heartbeat between targets, so
// a pass this long is not mistaken for a stalled sender.
#define GSHEETS_V2_TIMEOUT_MS 60000

// An acknowledgement for a full batch is ~25 bytes per record plus a small envelope, so
// 1024 covers the default batch several times over. Kept deliberately small because this
// buffer is live while mbedTLS is trying to allocate its own 16 KB handshake buffers.
// A larger error page from Apps Script simply parses as malformed, which is handled.
#define GSHEETS_V2_RESPONSE_SIZE 1024

// After a fully successful batch with more still queued, retry quickly so a long backlog
// drains in minutes rather than at the normal 10-minute cadence.
#define GSHEETS_V2_DRAIN_DELAY_SEC 5

bool dataSendHandler::send_to_google_v2()
{
    if (!send_gSheets && !send_backlog_now)
        return true;

    SenderLock lock(TARGET_GOOGLE_SHEETS, GSHEETS_V2_TIMEOUT_MS);
    if (!lock)
        return false;   // sender busy; retry next pass

    send_gSheets = false;
    send_backlog_now = false;

    if (strlen(config.scriptsURL) < GSCRIPTS_MIN_URL_LENGTH ||
        strlen(config.scriptsEmail) < GSCRIPTS_MIN_EMAIL_LENGTH) {
        queueUploadState = QueueUploadState::DISABLED;
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return true;
    }

    if (!reading_queue.isHealthy()) {
        queueUploadState = QueueUploadState::DISABLED;
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return false;
    }

    size_t batchSize = config.queueBatchSize;
    if (batchSize == 0) batchSize = 1;

    // Heap, not stack: loopTask has an 8 KB stack and this is ~2.5 KB of records plus a
    // JSON document of similar size.
    QueuedReading *batch = (QueuedReading *)malloc(batchSize * sizeof(QueuedReading));
    if (batch == nullptr) {
        Log.error("GSheets v2: unable to allocate a %u-record batch buffer.\r\n", (unsigned)batchSize);
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return false;
    }

    /*
     * A backlog is always drained first, in order, before anything live goes out - the
     * queue holds older captures and the sheet reads better chronologically.
     *
     * With no backlog the readings are taken live and never touch flash: they are built
     * here, sent, and dropped on acknowledgement. Persistence only happens once this stops
     * working, from take_queue_snapshot() on its own interval, which is what lets an
     * outage be recorded coarsely (long runway) while healthy operation stays fine-grained.
     */
    const bool fromQueue = reading_queue.pendingCount() > 0;

    size_t count;
    if (fromQueue) {
        count = reading_queue.peekBatch(batch, batchSize);
    } else {
        count = collectCurrentReadings(batch, (uint16_t)batchSize);

        // More Tilts than fit in one request. Sending a partial live batch would silently
        // drop the rest, so fall back to the queue for this pass: the snapshot captures
        // every Tilt and the drain above sends them batchSize at a time.
        if (count > 0 && tilt_scanner.m_tilt_devices.size() > batchSize) {
            free(batch);
            Log.warning("GSheets v2: %u Tilts exceed queueBatchSize %u; queueing instead of sending live. "
                        "Raise queueBatchSize to send them in one request.\r\n",
                        (unsigned)tilt_scanner.m_tilt_devices.size(), (unsigned)batchSize);
            snapshot_due = true;
            rearmGSheetsTimer(config.gsheetsPushEvery);
            return true;
        }
    }

    if (count == 0) {
        free(batch);
        queueUploadState = QueueUploadState::IDLE;
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return true;
    }

    queueUploadState = QueueUploadState::SENDING;

    /*
     * Record ids are kept because `batch` is freed before the TLS handshake, and the live
     * path has no queue to acknowledge against - it has to match the returned ids itself.
     */
    char (*sentIds)[QR_RECORD_ID_LEN] = nullptr;
    if (!fromQueue) {
        sentIds = (char (*)[QR_RECORD_ID_LEN])malloc(count * QR_RECORD_ID_LEN);
        if (sentIds == nullptr) {
            free(batch);
            rearmGSheetsTimer(config.gsheetsPushEvery);
            return false;
        }
        for (size_t i = 0; i < count; i++)
            qr_format_record_id(batch[i], sentIds[i], QR_RECORD_ID_LEN);
    }

    // ---- Build the request ----
    JsonDocument payload;
    payload["schemaVersion"] = 2;
    payload["deviceName"] = config.mdnsID;
    payload["Email"] = config.scriptsEmail;
    payload["tzOffset"] = config.TZoffset;

    JsonArray readings = payload["readings"].to<JsonArray>();

    for (size_t i = 0; i < count; i++) {
        const QueuedReading &r = batch[i];

        char recordId[QR_RECORD_ID_LEN];
        qr_format_record_id(r, recordId, sizeof(recordId));

        JsonObject o = readings.add<JsonObject>();
        o["recordId"] = recordId;
        o["deviceId"] = r.deviceId;
        o["mac"] = r.deviceId;
        o["Beer"] = r.sheetName;
        o["Color"] = (r.colorIndex < TILT_COLORS) ? tilt_color_names[r.colorIndex] : "Unknown";
        o["Temp"] = r.tempF;
        o["SG"] = r.gravity;
        o["SG_Raw"] = r.gravityRaw;
        o["SG_Smoothed"] = r.gravitySmoothed;
        o["RSSI"] = r.rssiLatest;
        o["RSSI_Avg"] = r.rssiAverage;
        o["RSSI_Min"] = r.rssiMinimum;
        o["RSSI_Max"] = r.rssiMaximum;
        o["RSSI_Samples"] = r.rssiSamples;

        const bool tsValid = (r.flags & QR_FLAG_TIMESTAMP_VALID) != 0 && r.capturedAtUtc != 0;
        o["TimestampValid"] = tsValid;

        if (tsValid) {
            char iso[24];
            format_utc_iso8601(r.capturedAtUtc, iso, sizeof(iso));
            o["CapturedAtUtc"] = iso;
        } else {
            // No trustworthy clock at capture. Send no timestamp at all rather than a
            // fabricated one, and give the server something to order by.
            o["UptimeMsAtCapture"] = r.capturedAtUptimeMs;
        }

        o["Comment"] = "";
    }

    const size_t payloadLen = measureJson(payload);
    char *payloadStr = (char *)malloc(payloadLen + 1);
    if (payloadStr == nullptr) {
        Log.error("GSheets v2: unable to allocate a %u-byte payload buffer.\r\n", (unsigned)(payloadLen + 1));
        free(batch);
        free(sentIds);
        queueUploadState = QueueUploadState::RETRYING;
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return false;
    }
    serializeJson(payload, payloadStr, payloadLen + 1);
    payload.clear();

    // Release the record buffer before opening the TLS connection. Acknowledgement is
    // driven entirely by the record ids the server echoes back, so nothing here is needed
    // again - and the mbedTLS handshake needs a large contiguous allocation
    // (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN is 16 KB) that fails on a fragmented heap.
    QueuedReading *batchForRssi = nullptr;
    if (!fromQueue) {
        // Only the deviceIds are needed afterwards, but the records are small and this
        // keeps the reset honest about which Tilts were actually in the request.
        batchForRssi = (QueuedReading *)malloc(count * sizeof(QueuedReading));
        if (batchForRssi != nullptr)
            memcpy(batchForRssi, batch, count * sizeof(QueuedReading));
    }

    free(batch);
    batch = nullptr;

    // Free heap alone is not the constraint here - the mbedTLS handshake needs large
    // contiguous blocks, so log the largest block too when diagnosing TLS alloc failures.
    Log.notice("GSheets v2: uploading %u queued reading%s (%u bytes). Heap free %u, largest block %u.\r\n",
               (unsigned)count, (count == 1) ? "" : "s", (unsigned)payloadLen,
               (unsigned)esp_get_free_heap_size(),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // ---- Send ----
    char *response = (char *)malloc(GSHEETS_V2_RESPONSE_SIZE);
    if (response == nullptr) {
        free(payloadStr);
        free(sentIds);
        free(batchForRssi);
        queueUploadState = QueueUploadState::RETRYING;
        rearmGSheetsTimer(config.gsheetsPushEvery);
        return false;
    }
    response[0] = '\0';

    HttpRequestOptions options;
    options.contentType = content_json;
    options.skipCertValidation = true;
    options.timeoutMs = GSHEETS_V2_TIMEOUT_MS;

    // BLE and WiFi share the radio, and a TLS handshake is far more round-trip sensitive
    // than the plain-HTTP targets. Hold scanning off for the duration of the upload; a
    // missed scan window costs nothing, since readings are already queued.
    tilt_scanner.pauseScanning();
    const uint32_t startedMs = sh_millis();

    int16_t httpCode = 0;
    const sendResult res = http_request(config.scriptsURL, httpMethod::HTTP_POST,
                                        payloadStr, response, GSHEETS_V2_RESPONSE_SIZE,
                                        options, &httpCode);

    const uint32_t elapsedMs = sh_millis() - startedMs;
    tilt_scanner.resumeScanning();
    Log.notice("GSheets v2: request took %u ms (http %d).\r\n", (unsigned)elapsedMs, (int)httpCode);

    free(payloadStr);

    bool result = false;
    size_t acceptedCount = 0;

    if (res != sendResult::success) {
        // Covers HTTP errors, timeouts, and a successful POST whose response was lost.
        // Nothing is acknowledged, so the identical batch - with identical record ids -
        // goes out again and the server's duplicate suppression absorbs it.
        Log.error("GSheets v2: upload failed (http %d). Nothing acknowledged; will retry the same records.\r\n",
                  (int)httpCode);

        setTargetStatus(TARGET_GOOGLE_SHEETS,
                        httpCode != 0 ? httpCodeToSendError(httpCode) : SEND_ERR_CONNECTION_FAILED);
        queueUploadState = QueueUploadState::RETRYING;

        /*
         * A live reading exists only in this buffer, so persist it now rather than dropping
         * it and waiting for the next snapshot.
         *
         * THESE records, not freshly captured ones. "Failed" includes a POST the server
         * actually processed whose response was lost, and re-sending the identical record id
         * is what lets the script's duplicate suppression absorb that. Capturing fresh values
         * instead would mint a new id and write a second, near-identical row.
         *
         * This only fires once per outage: it leaves the queue non-empty, so every later pass
         * takes the drain path above and persistence reverts to take_queue_snapshot() on its
         * configured interval - which is what keeps the queue growing slowly.
         */
        if (!fromQueue && batchForRssi != nullptr && reading_queue.isHealthy()) {
            uint16_t stored = 0;
            for (size_t i = 0; i < count; i++) {
                if (reading_queue.append(batchForRssi[i])) {
                    resetCollectedRssiIntervals(&batchForRssi[i], 1);
                    stored++;
                }
            }

            if (stored > 0)
                Log.notice("GSheets v2: persisted %u undelivered reading%s to the queue.\r\n",
                           (unsigned)stored, (stored == 1) ? "" : "s");
        }
    } else {
        JsonDocument reply;
        const DeserializationError err = deserializeJson(reply, response);

        if (err) {
            // A 200 with an unparseable body (typically an Apps Script HTML error page)
            // acknowledges nothing.
            response[255] = '\0';
            Log.error("GSheets v2: response was not valid JSON (%s). Body starts: %s\r\n",
                      err.c_str(), response);
            setTargetStatus(TARGET_GOOGLE_SHEETS, SEND_ERR_OTHER);
            queueUploadState = QueueUploadState::RETRYING;
        } else if (!reply["acceptedRecordIds"].is<JsonArrayConst>()) {
            Log.error("GSheets v2: response has no acceptedRecordIds array; treating as unacknowledged. "
                      "Has the Apps Script been updated for schemaVersion 2?\r\n");
            setTargetStatus(TARGET_GOOGLE_SHEETS, SEND_ERR_OTHER);
            queueUploadState = QueueUploadState::RETRYING;
        } else {
            for (JsonVariantConst v : reply["acceptedRecordIds"].as<JsonArrayConst>()) {
                const char *id = v.as<const char *>();
                if (id == nullptr)
                    continue;

                if (fromQueue) {
                    if (reading_queue.acknowledgeId(id))
                        acceptedCount++;
                } else {
                    // Live records were never stored, so acceptance is simply "delivered".
                    for (size_t i = 0; i < count; i++) {
                        if (strcmp(sentIds[i], id) == 0) {
                            acceptedCount++;
                            break;
                        }
                    }
                }
            }

            if (fromQueue)
                reading_queue.compact();

            result = true;
            lastQueueUploadSuccessMs = sh_millis();
            setTargetStatus(TARGET_GOOGLE_SHEETS, SEND_OK);

            if (acceptedCount < count) {
                // Queued records simply stay queued and go out again under the same ids.
                // Live ones have nowhere to go, so persist instead of dropping them - the
                // snapshot captures every Tilt, including the ones that did not land.
                Log.warning("GSheets v2: server accepted %u of %u records; %u will be retried.\r\n",
                            (unsigned)acceptedCount, (unsigned)count, (unsigned)(count - acceptedCount));
                if (!fromQueue)
                    snapshot_due = true;
            } else {
                Log.notice("GSheets v2: all %u records accepted (%u still queued).\r\n",
                           (unsigned)acceptedCount, (unsigned)reading_queue.pendingCount());
            }

            queueUploadState = (reading_queue.pendingCount() > 0)
                             ? QueueUploadState::SENDING
                             : QueueUploadState::IDLE;
        }
    }

    // The RSSI aggregation window restarts only now, and only for live records that were
    // actually delivered. Queued ones reset when they were written to flash.
    if (!fromQueue && result && acceptedCount == count && batchForRssi != nullptr)
        resetCollectedRssiIntervals(batchForRssi, (uint16_t)count);

    free(batchForRssi);
    free(sentIds);
    free(response);

    // Drain a backlog quickly, but only when the last batch actually made progress -
    // otherwise fall back to the normal cadence so a failing server is not hammered.
    const bool madeProgress = result && acceptedCount > 0;
    if (madeProgress && reading_queue.pendingCount() > 0)
        rearmGSheetsTimer(GSHEETS_V2_DRAIN_DELAY_SEC);
    else
        // Backoff applies only here: a run of failures (a stale Apps Script, a dead
        // endpoint) should stop claiming the sender on its configured interval. The drain
        // path above is only reached on success, so it is never throttled.
        rearmGSheetsTimer(backoffDelay(TARGET_GOOGLE_SHEETS, config.gsheetsPushEvery));

    return result;
}
