import { describe, test, expect } from '@jest/globals';
import { normalizeEventBusMetrics } from '../src/js/codeMetricsUtils.js';

describe('normalizeEventBusMetrics', () => {
  test('handles legacy structure', () => {
    const input = {
      dropped_total: 10,
      dropped_by_consumer: [
        { name: 'mqtt', dropped: 4 },
        { name: 'logger', dropped: 6 },
      ],
      queue_depth: [
        { name: 'mqtt', used: 3, capacity: 8 },
        { name: 'logger', used: 2, capacity: 6 },
      ],
    };

    const result = normalizeEventBusMetrics(input);

    expect(result.droppedTotal).toBe(10);
    expect(result.blockingTotal).toBe(0);
    expect(result.consumers).toEqual([
      { name: 'logger', dropped: 6, blocking: 0 },
      { name: 'mqtt', dropped: 4, blocking: 0 },
    ]);
    expect(result.queueDepth).toEqual([
      { name: 'mqtt', used: 3, capacity: 8 },
      { name: 'logger', used: 2, capacity: 6 },
    ]);
  });

  test('merges blocking data from nested payload', () => {
    const input = {
      dropped: {
        total: 12,
        by_consumer: {
          mqtt: { dropped: 7 },
          websocket: { dropped: 5 },
        },
      },
      blocking: {
        total: 3,
        by_consumer: [
          { name: 'mqtt', blocking: 2 },
          { name: 'websocket', blocking: 1 },
        ],
      },
      queues: {
        by_consumer: [
          { name: 'mqtt', used: 4, capacity: 12 },
          { name: 'websocket', used: 6, capacity: 16 },
        ],
      },
    };

    const result = normalizeEventBusMetrics(input);

    expect(result.droppedTotal).toBe(12);
    expect(result.blockingTotal).toBe(3);
    expect(result.consumers).toEqual([
      { name: 'mqtt', dropped: 7, blocking: 2 },
      { name: 'websocket', dropped: 5, blocking: 1 },
    ]);
  });
});
