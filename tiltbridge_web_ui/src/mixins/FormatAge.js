/**
 * Shared formatting for the raw "age in seconds" values the firmware APIs return.
 *
 * The device has no shared clock with the browser, so /api/queue/ and /api/sender/ report
 * ages rather than timestamps. Three separate panels render those, so the formatting lives
 * here rather than being duplicated per component.
 *
 * Returns null when there is no value to show (null/undefined/non-numeric), which callers
 * render as "Never".
 */
export function formatAge(seconds) {
    if (seconds === null || seconds === undefined || seconds === "") {
        return null;
    }

    const total = Number(seconds);
    if (!Number.isFinite(total)) {
        return null;
    }

    const whole = Math.max(0, Math.floor(total));
    const days = Math.floor(whole / 86400);
    const hours = Math.floor((whole % 86400) / 3600);
    const minutes = Math.floor((whole % 3600) / 60);
    const secs = whole % 60;

    if (days > 0) {
        return hours > 0 ? `${days}d ${hours}h` : `${days}d`;
    }
    if (hours > 0) {
        return minutes > 0 ? `${hours}h ${minutes}m` : `${hours}h`;
    }
    if (minutes > 0) {
        return secs > 0 ? `${minutes}m ${secs}s` : `${minutes}m`;
    }
    return `${secs}s`;
}

/**
 * Millisecond variant, used for the sender recovery record (which reports ms).
 */
export function formatAgeMs(milliseconds) {
    if (milliseconds === null || milliseconds === undefined || milliseconds === "") {
        return null;
    }
    const ms = Number(milliseconds);
    if (!Number.isFinite(ms)) {
        return null;
    }
    return formatAge(ms / 1000);
}
