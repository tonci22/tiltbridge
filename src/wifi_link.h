#ifndef TILTBRIDGE_WIFI_LINK_H
#define TILTBRIDGE_WIFI_LINK_H

#include <cstdint>
#include <ArduinoJson.h>

/**
 * @brief Health of the WiFi uplink: signal strength plus the stability history behind it.
 *
 * Signal strength on its own is a poor answer to "is the WiFi good?". A -50 dBm link to an
 * access point that de-authenticates every ninety seconds is a bad link; a steady -80 dBm
 * one is fine. So this tracks both, and the UI shows them together.
 *
 * Every reading comes from esp_wifi_sta_get_ap_info() - the driver - and never from
 * wifi_cfg_get_status(). The manager only populates its RSSI field while its own state says
 * CONNECTED, and this fork exists in large part because that flag is known to sit
 * stale-false for hours while the link is demonstrably fine (see network_is_usable() and
 * reconnectWiFi() in wifi_setup.cpp). Sourcing the indicator from the manager would blank
 * it during precisely the failure it is meant to make visible.
 */

/**
 * @brief Take an RSSI sample if one is due. Call from loop(); self-throttling.
 */
void wifi_link_sample();

/**
 * @brief An outage has begun. Call once per outage, not once per reconnect attempt.
 */
void wifi_link_note_outage_started();

/**
 * @brief The link is up and carrying traffic (got-IP). Ends any outage in progress.
 */
void wifi_link_note_connected();

/**
 * @brief Move off a poor access point when a materially better one exists.
 *
 * Call from loop(); self-throttling, and rate limited to one attempt an hour. Blocks the
 * calling task for up to about twelve seconds when it does act - see the rationale block in
 * wifi_link.cpp for why that is safe and why this is needed at all.
 */
void wifi_link_check_ap();

/**
 * @brief Emit the link report. Shared by GET /api/json/ and GET /api/network/ so the
 *        homepage indicator and the About panel can never disagree with each other.
 */
void wifi_link_json(JsonObject o);

#endif // TILTBRIDGE_WIFI_LINK_H
