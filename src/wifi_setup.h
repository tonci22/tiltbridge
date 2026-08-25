#ifndef TILTBRIDGE_WIFI_SETUP_H
#define TILTBRIDGE_WIFI_SETUP_H

#include <stddef.h>
#include <stdint.h>

#define WIFI_SETUP_AP_NAME "TiltBridgeAP"
#define WIFI_SETUP_AP_PASS "tiltbridge" // Must be 8-63 chars

#define WEB_SERVER_PORT 80


void initWiFi();

void disconnectWiFi();
void reconnectWiFi();

// ESP-IDF compatible WiFi status check. Thin wrapper over the esp_wifi_config
// manager's own connected flag.
bool is_wifi_connected();

/**
 * @brief True when the STA interface can actually carry traffic.
 *
 * Prefer this over is_wifi_connected() for gating outbound work. It trusts the
 * esp_wifi_config manager when the manager says "connected", but when the manager says
 * "disconnected" it verifies against the netif before believing it. A stale-false flag
 * there silently disabled every outbound target while BLE and the web UI kept running -
 * the failure this fork exists to fix.
 */
bool network_is_usable();

/**
 * @brief How often, and for how long, the WiFi manager's flag has disagreed with a
 *        demonstrably-up interface.
 *
 * Episodes rather than polls. The predecessor counted every network_is_usable() call, which
 * runs ~100x a second from the sender loop, so one minute of disagreement reported as
 * "5,984" - a duration in units of 10 ms masquerading as a fault count.
 *
 * A non-zero `episodes` still confirms the stale-flag diagnosis; `longestMs` is the figure
 * that says whether it mattered.
 */
struct WifiDesyncStats {
    uint32_t episodes  = 0;   // distinct episodes since boot
    uint32_t currentMs = 0;   // 0 = the flag currently agrees with the interface
    uint32_t longestMs = 0;
    uint64_t totalMs   = 0;
};

WifiDesyncStats wifi_desync_stats();

// ESP-IDF compatible local IP address retrieval
// Writes the IP address as a string (e.g., "192.168.1.100") to ip_str
// Returns true if successful, false if not connected
bool get_local_ip(char* ip_str, size_t len);

#endif //TILTBRIDGE_WIFI_SETUP_H
