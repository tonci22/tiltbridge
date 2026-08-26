#include <thorlog.h>

#include "jsonconfig.h"
#include "sendData.h"
#include "sender_health.h"
#include "tilt/tiltScanner.h"
#include "targets/send_json_str.h"


bool dataSendHandler::send_to_legacy_fermentrack()
{
    bool result = true;

    if (send_legacy_fermentrack)
    {
        SenderLock lock(TARGET_LEGACY_FERMENTRACK, HTTP_TIMEOUT_DEFAULT_MS);
        if (!lock)
            return result;

        // Fermentrack
        send_legacy_fermentrack = false;
//        tilt_scanner.deinit();

        if (strlen(config.legacyFermentrackURL) >= FERMENTRACK_MIN_URL_LENGTH) {
            Log.verbose("Calling send to Legacy Fermentrack.\r\n");

            /*
             * Measured heap buffer, not `char tilt_data[TILT_ALL_DATA_SIZE + 128]`.
             *
             * That array was 4,015 bytes (477 * 8 + 71 + 128) of the 8 KB loopTask stack,
             * and it was still live across http_request(), which adds its own frame plus
             * esp_http_client_perform()'s underneath it - about 4.7 KB of the 8 KB in one
             * call chain, on the task that also runs every other sender.
             *
             * Same measure, allocate, verify shape as fermentrack_2.cpp. The documents are
             * scoped so they are destroyed before the request goes out, and the payload is
             * the only thing still held.
             */
            char *tilt_data = nullptr;
            bool payload_ok = false;

            {
                JsonDocument doc;

                // Load the Tilt data from the scanner
                JsonDocument tilt_doc;
                // This is the only call to tilt_to_json_legacy
                // The main difference vs tilt_to_json is that it sends a dict with the color as the key rather than an array.
                // When we discontinue Legacy Fermentrack support this can also be discontinued
                // This also only ever sends raw gravity
                tilt_scanner.tilt_to_json_legacy(tilt_doc);

                doc["mdns_id"] = config.mdnsID;
                doc["tilts"] = tilt_doc;

                const size_t payload_size = measureJson(doc) + 1;
                tilt_data = (char *)malloc(payload_size);

                if (tilt_data == nullptr) {
                    Log.error("Legacy Fermentrack: unable to allocate %u bytes for the payload.\r\n",
                              (unsigned)payload_size);
                } else {
                    const size_t written = serializeJson(doc, tilt_data, payload_size);
                    if (written + 1 == payload_size) {
                        payload_ok = true;
                    } else {
                        // Never expected now that the buffer is measured, but a silent
                        // truncation is precisely what the fixed-size array risked once
                        // eight Tilts were reporting - fail loudly instead.
                        Log.error("Legacy Fermentrack: payload truncated (%u of %u bytes); not sending.\r\n",
                                  (unsigned)written, (unsigned)(payload_size - 1));
                        free(tilt_data);
                        tilt_data = nullptr;
                    }
                }
            }

            if (payload_ok) {
                int16_t httpCode = 0;
                if (http_request(config.legacyFermentrackURL, httpMethod::HTTP_POST, tilt_data, &httpCode) == sendResult::success)
                {
                    Log.notice("Completed send to Legacy Fermentrack.\r\n");
                }
                else
                {
                    result = false; // There was an error with the previous send
                    Log.verbose("Error sending to Legacy Fermentrack.\r\n");
                }
                data_sender.setTargetStatus(TARGET_LEGACY_FERMENTRACK, dataSendHandler::httpCodeToSendError(httpCode));
                free(tilt_data);
            } else {
                // Nothing was sent, so this counts as a failed cycle like any other.
                result = false;
                data_sender.setTargetStatus(TARGET_LEGACY_FERMENTRACK, SEND_ERR_OTHER);
            }
        }
        data_sender.startTimer(data_sender.legacyFermentrackTimer, data_sender.backoffDelay(TARGET_LEGACY_FERMENTRACK, config.legacyFermentrackPushEvery)); // Set up subsequent send to Fermentrack
//        tilt_scanner.init();
    }
    return result;
}