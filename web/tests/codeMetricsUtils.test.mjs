import test from 'node:test';
import assert from 'node:assert/strict';

import { normalizeEventBusMetrics } from '../src/js/codeMetricsUtils.js';

test('normalizeEventBusMetrics handles legacy structure', () => {
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
  assert.equal(result.droppedTotal, 10);
  assert.equal(result.blockingTotal, 0);
  assert.deepEqual(result.consumers, [
    { name: 'logger', dropped: 6, blocking: 0 },
    { name: 'mqtt', dropped: 4, blocking: 0 },
  ]);
  assert.deepEqual(result.queueDepth, [
    { name: 'mqtt', used: 3, capacity: 8 },
    { name: 'logger', used: 2, capacity: 6 },
  ]);
});

test('normalizeEventBusMetrics merges blocking data from nested payload', () => {
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
  assert.equal(result.droppedTotal, 12);
  assert.equal(result.blockingTotal, 3);
  assert.deepEqual(result.consumers, [
    { name: 'mqtt', dropped: 7, blocking: 2 },
    { name: 'websocket', dropped: 5, blocking: 1 },
  ]);
});
