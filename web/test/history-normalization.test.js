import { describe, expect, test } from '@jest/globals';
import { normalizeSample, parseHistoryResponse } from '../src/js/utils/history.js';

describe('History response helpers', () => {
    test('normalizeSample fills derived fields and normalizes keys', () => {
        const baseTimestamp = 1_700_000_000_000;
        const sample = {
            timestamp: baseTimestamp,
            pack_voltage_v: '52.75',
            pack_current: -12.5,
            state_of_charge_pct: '80.5',
            state_of_health: 97.2,
            average_temperature_c: '24.3',
        };

        const normalized = normalizeSample(sample);

        expect(normalized.timestamp).toBe(baseTimestamp);
        expect(normalized.timestamp_ms).toBe(baseTimestamp);
        expect(normalized.timestamp_iso).toBe(new Date(baseTimestamp).toISOString());
        expect(normalized.pack_voltage).toBeCloseTo(52.75);
        expect(normalized.pack_current).toBeCloseTo(-12.5);
        expect(normalized.state_of_charge).toBeCloseTo(80.5);
        expect(normalized.state_of_health).toBeCloseTo(97.2);
        expect(normalized.average_temperature).toBeCloseTo(24.3);
    });

    test('parseHistoryResponse supports `samples` payloads with metadata', () => {
        const firstTimestamp = 1_700_000_100_000;
        const secondTimestamp = firstTimestamp + 60_000;
        const payload = {
            total: '512',
            interval_ms: 60_000,
            samples: [
                {
                    timestamp: firstTimestamp,
                    pack_voltage: 52.1,
                    pack_current: -11.2,
                },
                {
                    timestamp_ms: secondTimestamp,
                    timestamp_iso: '2023-11-13T12:00:00.000Z',
                    pack_voltage_v: '52.4',
                    pack_current_a: '8.5',
                },
            ],
        };

        const { samples, total, metadata } = parseHistoryResponse(payload);

        expect(total).toBe(512);
        expect(samples).toHaveLength(2);
        expect(metadata).toMatchObject({
            total: 512,
            returned: 2,
            interval_ms: 60_000,
        });
        expect(samples[0]).toMatchObject({
            timestamp: firstTimestamp,
            pack_voltage: 52.1,
            pack_current: -11.2,
        });
        expect(samples[0].timestamp_iso).toBe(new Date(firstTimestamp).toISOString());
        expect(samples[1]).toMatchObject({
            timestamp_ms: secondTimestamp,
            pack_voltage: 52.4,
            pack_current: 8.5,
        });
        expect(samples[1].timestamp_iso).toBe('2023-11-13T12:00:00.000Z');
    });

    test('parseHistoryResponse falls back to legacy `entries` payloads', () => {
        const now = Date.now();
        const payload = {
            count: 1,
            entries: [
                {
                    timestamp_ms: now,
                    pack_voltage_v: 51.2,
                    pack_current_a: -7.8,
                },
            ],
        };

        const { samples, total, metadata } = parseHistoryResponse(payload);

        expect(total).toBe(1);
        expect(samples).toHaveLength(1);
        expect(metadata).toMatchObject({ total: 1, returned: 1 });
        expect(samples[0].timestamp_ms).toBe(now);
        expect(samples[0].pack_voltage).toBeCloseTo(51.2);
        expect(samples[0].pack_current).toBeCloseTo(-7.8);
    });
});
