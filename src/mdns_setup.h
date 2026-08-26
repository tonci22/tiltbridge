#ifndef TILTBRIDGE_MDNS_SETUP_H
#define TILTBRIDGE_MDNS_SETUP_H

// Initialize mDNS responder and register services.
// Call after WiFi is connected and config.mdnsID is set.
void initMDNS();

// Tear down and re-initialize mDNS with the current config.mdnsID.
//
// Must NOT be called from a handler running on the default event loop - it re-registers
// esp_event handlers, which that loop cannot remove inline. Use mdnsRequestReset() there.
void mdnsReset();

// Ask for a reset from a context that cannot perform one, notably a WiFi event handler.
void mdnsRequestReset();

// Perform a reset asked for by mdnsRequestReset(). Call from loopTask; does nothing if
// none is pending.
void mdnsServicePendingReset();

#endif //TILTBRIDGE_MDNS_SETUP_H
