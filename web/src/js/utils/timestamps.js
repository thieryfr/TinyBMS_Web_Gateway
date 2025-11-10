/**
 * Utilities for normalizing timestamp fields coming from TinyBMS/Victron payloads.
 * They ensure callers rely on the hardware-provided millisecond timestamps while
 * preserving legacy `timestamp` fields for backward compatibility.
 */

function toFinitePositiveNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) && number > 0 ? number : 0;
}

function parseIsoTimestamp(iso) {
    if (typeof iso !== 'string' || iso.length === 0) {
        return 0;
    }
    const parsed = Date.parse(iso);
    return Number.isNaN(parsed) ? 0 : parsed;
}

/**
 * Resolve timestamp fields from an arbitrary payload.
 *
 * @param {object} source
 * @param {number} [fallback=0] Fallback timestamp (typically Date.now())
 * @returns {{ timestamp: number, timestamp_ms: number, timestamp_iso: string | null }}
 */
export function resolveTimestampFields(source, fallback = 0) {
    if (!source || typeof source !== 'object') {
        const resolvedFallback = toFinitePositiveNumber(fallback);
        return {
            timestamp: resolvedFallback,
            timestamp_ms: resolvedFallback,
            timestamp_iso: null,
        };
    }

    const iso = typeof source.timestamp_iso === 'string' && source.timestamp_iso.length > 0
        ? source.timestamp_iso
        : null;

    let timestampMs =
        toFinitePositiveNumber(source.timestamp_ms) ||
        toFinitePositiveNumber(source.timestampMs) ||
        0;

    let timestamp = toFinitePositiveNumber(source.timestamp);

    if (timestampMs === 0) {
        timestampMs = timestamp || parseIsoTimestamp(iso);
    }

    if (timestamp === 0) {
        timestamp = timestampMs || parseIsoTimestamp(iso);
    }

    const resolvedFallback = toFinitePositiveNumber(fallback);

    if (timestampMs === 0 && resolvedFallback > 0) {
        timestampMs = resolvedFallback;
    }

    if (timestamp === 0) {
        timestamp = timestampMs || resolvedFallback;
    }

    return {
        timestamp,
        timestamp_ms: timestampMs,
        timestamp_iso: iso,
    };
}

/**
 * Convenience helper returning a numeric timestamp in milliseconds, prioritising
 * hardware-provided values when available.
 *
 * @param {object} source
 * @param {number} [fallback=0]
 * @returns {number}
 */
export function resolveTimestampMs(source, fallback = 0) {
    return resolveTimestampFields(source, fallback).timestamp_ms;
}

/**
 * Convenience helper returning the most suitable timestamp (legacy `timestamp`
 * when present, otherwise the millisecond value).
 *
 * @param {object} source
 * @param {number} [fallback=0]
 * @returns {number}
 */
export function resolveTimestamp(source, fallback = 0) {
    return resolveTimestampFields(source, fallback).timestamp;
}

export default resolveTimestampFields;
