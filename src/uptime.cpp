//
// Created by Lee Bussy on 12/31/20
//

#include <esp_timer.h>

#include "uptime.h"

/*
 * esp_timer_get_time() is a 64-bit microsecond counter, so this does not wrap in any
 * practical service life. The previous implementation went through a 32-bit millis()
 * shim, which wraps at ~49.7 days.
 */
static inline uint64_t uptimeMillisTotal() {
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

/*
 * The previous version cached the five components in statics and refreshed them through
 * getNow()/setValues(). Two things were wrong with it:
 *
 *   - getNow() reassigned the cached timestamp on EVERY call, so the "refresh once a
 *     second" test could never expire and setValues() effectively never ran. Callers using
 *     the default refr = false therefore received stale values.
 *   - Each accessor re-sampled the clock before computing its own component, so a set of
 *     them was not one point in time. /api/uptime/ was seen reporting 2m59s then 2m0s -
 *     seconds past a rollover, minutes before it.
 *
 * Deriving everything from a single read is cheaper than the cache it replaces, so there
 * is nothing to gain by keeping one.
 */
UptimeParts uptimeSnapshot()
{
    const uint64_t now = uptimeMillisTotal();   // one read; every field below derives from it

    UptimeParts p;
    p.totalSeconds = (uint32_t)(now / SEC_MILLIS);
    p.days    = (int)(now / DAY_MILLIS);
    p.hours   = (int)((now % DAY_MILLIS) / HOUR_MILLIS);
    p.minutes = (int)((now % HOUR_MILLIS) / MIN_MILLIS);
    p.seconds = (int)((now % MIN_MILLIS) / SEC_MILLIS);
    p.millis  = (int)(now % SEC_MILLIS);
    return p;
}

uint32_t uptimeTotalSeconds()
{
    return uptimeSnapshot().totalSeconds;
}

/*
 * The refr parameter is retained so existing call sites still compile. It no longer does
 * anything: every accessor is now always current, because there is no cache to refresh.
 */
int uptimeDays(bool)    { return uptimeSnapshot().days; }
int uptimeHours(bool)   { return uptimeSnapshot().hours; }
int uptimeMinutes(bool) { return uptimeSnapshot().minutes; }
int uptimeSeconds(bool) { return uptimeSnapshot().seconds; }
int uptimeMillis(bool)  { return uptimeSnapshot().millis; }
