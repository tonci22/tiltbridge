#ifndef TILTBRIDGE_TIME_SYNC_H
#define TILTBRIDGE_TIME_SYNC_H

#include <cstddef>
#include <cstdint>

/**
 * Minimal SNTP client.
 *
 * Before this existed there was no time source anywhere in the firmware - std::time(0)
 * returned 1970-based values. Queued readings need a real capture time, and the spec is
 * explicit that a timestamp must never be fabricated: when no sync has landed, records
 * carry timestampValid = false rather than a guess.
 */

// Configure SNTP. Safe to call once during setup, before WiFi is up.
void time_sync_init();

// Begin syncing. Called when the STA interface first gets an IP.
void time_sync_start();

// True once a plausible absolute time has been obtained.
bool time_is_valid();

// Unix seconds UTC, or 0 when !time_is_valid().
uint32_t utc_now();

// "2026-08-10T08:40:00Z". Writes an empty string when unixSeconds is 0.
void format_utc_iso8601(uint32_t unixSeconds, char *out, size_t outSize);

#endif // TILTBRIDGE_TIME_SYNC_H
