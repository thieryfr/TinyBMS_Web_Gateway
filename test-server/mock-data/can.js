const CAN_BITRATE_BPS = 500000;
const OCCUPANCY_WINDOW_MS = 60000;

const state = {
  driverStarted: true,
  keepaliveOk: true,
  lastKeepaliveUpdate: Date.now(),
  lastTxMs: Date.now() - 250,
  lastRxMs: Date.now() - 100,
  txCount: 0,
  rxCount: 0,
  txBytes: 0,
  rxBytes: 0,
  txErrorCounter: 0,
  rxErrorCounter: 0,
  txFailedCount: 0,
  rxMissedCount: 0,
  arbitrationLostCount: 0,
  busErrorCount: 0,
  busOffCount: 0,
  busState: 1, // Running
  samples: [],
  lastUpdate: Date.now(),
};

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function simulateKeepalive(now) {
  const elapsed = now - state.lastKeepaliveUpdate;
  if (elapsed >= 1000) {
    state.lastKeepaliveUpdate = now;
    state.lastTxMs = now - 20;
    state.lastRxMs = now - 5 - Math.floor(Math.random() * 10);
    state.keepaliveOk = Math.random() > 0.02;

    if (!state.keepaliveOk && Math.random() < 0.1) {
      state.txFailedCount += 1;
    }
  }
}

function simulateTraffic(now) {
  const deltaMs = Math.max(now - state.lastUpdate, 0);
  state.lastUpdate = now;
  if (deltaMs === 0) {
    return;
  }

  const framesPerSecond = 25 + Math.random() * 30;
  const frameCount = Math.max(1, Math.round((framesPerSecond * deltaMs) / 1000));
  const txRatio = 0.4 + Math.random() * 0.4;
  const txFrames = Math.round(frameCount * txRatio);
  const rxFrames = frameCount - txFrames;
  const avgPayloadBytes = 6 + Math.floor(Math.random() * 3);

  state.txCount += txFrames;
  state.rxCount += rxFrames;
  state.txBytes += txFrames * avgPayloadBytes;
  state.rxBytes += rxFrames * avgPayloadBytes;

  const bitsPerFrame = 47 + avgPayloadBytes * 8;
  const totalBits = frameCount * bitsPerFrame;
  state.samples.push({ timestamp: now, bits: totalBits });

  while (state.samples.length > 0 && state.samples[0].timestamp < now - OCCUPANCY_WINDOW_MS) {
    state.samples.shift();
  }

  if (Math.random() < 0.02) {
    state.busErrorCount += 1;
  }
  if (Math.random() < 0.015) {
    state.arbitrationLostCount += 1;
  }
  if (Math.random() < 0.01) {
    state.rxMissedCount += 1;
  }

  if (Math.random() < 0.005) {
    state.busState = 2; // Bus-off
    state.busOffCount += 1;
  } else {
    state.busState = 1;
  }

  state.txErrorCounter = clamp(state.txErrorCounter + Math.floor(Math.random() * 2), 0, 255);
  state.rxErrorCounter = clamp(state.rxErrorCounter + Math.floor(Math.random() * 2), 0, 255);
}

function computeOccupancy() {
  if (state.samples.length === 0) {
    return 0;
  }

  const totalBits = state.samples.reduce((acc, sample) => acc + sample.bits, 0);
  const windowSeconds = OCCUPANCY_WINDOW_MS / 1000;
  const occupancy = (totalBits / (CAN_BITRATE_BPS * windowSeconds)) * 100;
  return clamp(occupancy, 0, 100);
}

function getStatus() {
  const now = Date.now();
  simulateKeepalive(now);
  simulateTraffic(now);

  return {
    timestamp_ms: now,
    driver_started: state.driverStarted,
    frames: {
      tx_count: state.txCount,
      rx_count: state.rxCount,
      tx_bytes: state.txBytes,
      rx_bytes: state.rxBytes,
    },
    keepalive: {
      ok: state.keepaliveOk,
      last_tx_ms: state.lastTxMs,
      last_rx_ms: state.lastRxMs,
      interval_ms: 1000,
      timeout_ms: 5000,
      retry_ms: 2000,
    },
    bus: {
      state: state.busState,
      state_label: state.busState === 2 ? 'Bus-off' : 'En marche',
      occupancy_pct: Number(computeOccupancy().toFixed(2)),
      window_ms: OCCUPANCY_WINDOW_MS,
    },
    errors: {
      tx_error_counter: state.txErrorCounter,
      rx_error_counter: state.rxErrorCounter,
      tx_failed_count: state.txFailedCount,
      rx_missed_count: state.rxMissedCount,
      arbitration_lost_count: state.arbitrationLostCount,
      bus_error_count: state.busErrorCount,
      bus_off_count: state.busOffCount,
    },
  };
}

module.exports = {
  getStatus,
};

