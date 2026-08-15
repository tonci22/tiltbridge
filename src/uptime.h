//
// Created by Lee Bussy on 12/31/20
//

#ifndef _UPTIME_H
#define _UPTIME_H

#include <stdint.h>

#define DAY_MILLIS 86400000
#define HOUR_MILLIS 3600000
#define MIN_MILLIS 60000
#define SEC_MILLIS 1000

/*
 * Every field is derived from ONE clock read, so a set of them is always internally
 * consistent.
 *
 * Reading the pieces through separate accessors is what made /api/uptime/ report 2m59s
 * and then 2m0s: each accessor re-sampled the clock, so minutes could be read before a
 * rollover and seconds after it. Anything that needs more than one field must take a
 * snapshot rather than call several accessors.
 */
struct UptimeParts {
    uint32_t totalSeconds;  // whole uptime in seconds - NOT the `seconds` field below
    int days;
    int hours;              // 0..23
    int minutes;            // 0..59
    int seconds;            // 0..59
    int millis;             // 0..999
};

UptimeParts uptimeSnapshot();

/*
 * Whole uptime in seconds.
 *
 * uptimeSeconds() below returns the SECONDS COMPONENT (0..59), which reads like total
 * uptime and is not. That mistake silently disabled the sender-health recovery reboot
 * (`uptimeSeconds() < 180` can never be false) and made /api/errors/'s last_attempt_at
 * wrap every minute. Use this for any elapsed-time arithmetic.
 */
uint32_t uptimeTotalSeconds();

int uptimeDays(bool refr = false);
int uptimeHours(bool refr = false);    // 0..23
int uptimeMinutes(bool refr = false);  // 0..59
int uptimeSeconds(bool refr = false);  // 0..59 - see uptimeTotalSeconds() above
int uptimeMillis(bool refr = false);   // 0..999

#endif // _UPTIME_H
