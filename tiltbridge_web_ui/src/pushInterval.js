/*
 * Bounds for the four target panels that take a push interval in SECONDS - MQTT, Brewstatus,
 * Taplist.io and InfluxDB.
 *
 * These mirror PUSH_EVERY_FAST_MIN_SEC and PUSH_EVERY_MAX_SEC in src/jsonconfig.h, which the
 * firmware now enforces on both the settings API and the config load path. A form that accepted
 * a wider range than the firmware would just produce a rejected save the user cannot explain -
 * which is what InfluxDB's own 60..86400 check did, since anything past 65535 is not even a
 * uint16_t and was refused outright.
 *
 * The other six targets use PushIntervalField instead, which works in minutes and carries its
 * own bounds, because their floor is ten minutes and nobody sets those in seconds.
 */
import { i18n } from '@/main'

export const PUSH_EVERY_FAST_MIN_SECONDS = 30;
export const PUSH_EVERY_MAX_SECONDS = 43200;

/**
 * Validate a push interval typed in seconds.
 *
 * @param {*} value raw field value - may be a String, because these are plain text inputs
 * @returns {{seconds: number, error: string}} error is "" when the value is usable, and
 *          seconds is then a Number, which is what the settings API requires: the firmware
 *          tests the JSON value with is<uint16_t>(), so a quoted "30" is rejected and takes
 *          every other field in the payload down with it.
 */
export function validatePushEverySeconds(value) {
    const seconds = parseInt(value, 10);

    if (!Number.isFinite(seconds) ||
        seconds < PUSH_EVERY_FAST_MIN_SECONDS ||
        seconds > PUSH_EVERY_MAX_SECONDS) {
        return {
            seconds: NaN,
            error: i18n.global.t('cloud_config.push_interval.error_out_of_range', {
                min: PUSH_EVERY_FAST_MIN_SECONDS,
                max: PUSH_EVERY_MAX_SECONDS,
            }),
        };
    }

    return { seconds, error: "" };
}
