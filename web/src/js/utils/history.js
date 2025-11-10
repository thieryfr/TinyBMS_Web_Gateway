import { resolveTimestampFields } from './timestamps.js';

function toFiniteNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
}

export function normalizeSample(raw) {
    const { timestamp, timestamp_ms, timestamp_iso } = resolveTimestampFields(raw);
    const isoTimestamp =
        timestamp_iso || (timestamp_ms > 0 ? new Date(timestamp_ms).toISOString() : null);

    return {
        timestamp,
        timestamp_ms,
        timestamp_iso: isoTimestamp,
        pack_voltage: Number(raw?.pack_voltage ?? raw?.pack_voltage_v ?? raw?.packVoltage ?? raw?.pack_voltage_V ?? 0),
        pack_current: Number(raw?.pack_current ?? raw?.pack_current_a ?? raw?.packCurrent ?? 0),
        state_of_charge: Number(raw?.state_of_charge ?? raw?.state_of_charge_pct ?? raw?.soc ?? 0),
        state_of_health: Number(raw?.state_of_health ?? raw?.state_of_health_pct ?? raw?.soh ?? 0),
        average_temperature: Number(raw?.average_temperature ?? raw?.average_temperature_c ?? raw?.temperature ?? 0),
    };
}

export function parseHistoryResponse(payload) {
    const data = payload && typeof payload === 'object' ? payload : {};
    const rawSamples = Array.isArray(data.samples)
        ? data.samples
        : Array.isArray(data.entries)
        ? data.entries
        : [];

    const samples = rawSamples.map((sample) => normalizeSample(sample));

    const total =
        toFiniteNumber(data.total) ??
        toFiniteNumber(data.count) ??
        toFiniteNumber(data.capacity) ??
        samples.length;

    const intervalMs = toFiniteNumber(data.interval_ms ?? data.intervalMs);
    const capacity = toFiniteNumber(data.capacity);

    const metadata = {
        total,
        returned: samples.length,
    };

    if (intervalMs && intervalMs > 0) {
        metadata.interval_ms = intervalMs;
    }

    if (capacity && capacity > 0) {
        metadata.capacity = capacity;
    }

    return { samples, total, metadata };
}

export default {
    normalizeSample,
    parseHistoryResponse,
};
