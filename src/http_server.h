#ifndef TILTBRIDGE_HTTP_SERVER_H
#define TILTBRIDGE_HTTP_SERVER_H

#include <esp_http_server.h>
#include <ArduinoJson.h>

// Every target's minimum credential length now lives with the senders that enforce them, in
// sendData.h. Two of the five that used to be here were dead, and the other three were second
// copies of the same numbers - the sort of duplication that let this endpoint's "is it
// configured" checks drift away from the senders' own in the first place.

class httpServer {
public:
    /**
     * @brief Initialize and start the HTTP server
     *
     * Sets up all routes, handlers, and starts the server
     */
    void init();

    /**
     * @brief Stop the HTTP server
     */
    void stop();

    // State flags set by HTTP handlers, processed by main loop
    bool lcd_reinit_rqd = false;
    bool restart_requested = false;
    bool name_reset_requested = false;
    bool wifi_reset_requested = false;
    bool factoryreset_requested = false;
    bool mqtt_init_rqd = false;
    bool queue_timer_restart_rqd = false;   // snapshot interval changed - re-arm the one-shot timer

private:
    /**
     * @brief Register all JSON API GET endpoints
     */
    void registerJsonGetHandlers();

    /**
     * @brief Register all JSON API PUT/POST endpoints
     */
    void registerJsonPutHandlers();

    /**
     * @brief Register calibration API endpoints
     */
    void registerCalibrationHandlers();

    /**
     * @brief Register action API endpoints (resetWifi, resetDevice)
     */
    void registerActionHandlers();

    /**
     * @brief Register per-device (MAC-keyed) Tilt configuration endpoints
     */
    void registerDeviceHandlers();
};

extern httpServer http_server;

#endif //TILTBRIDGE_HTTP_SERVER_H
