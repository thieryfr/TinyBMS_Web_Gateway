const COMMANDS = ['0x03', '0x04', '0x10', '0x11', '0x30', '0x31', '0x40'];
const ERROR_TYPES = ['CRC', 'Timeout', 'Framing', 'Overflow'];
const MAX_EVENTS = 50;
const MAX_HISTORY_POINTS = 120;

const state = {
  connected: true,
  startedAt: Date.now(),
  totalFrames: 0,
  totalBytes: 0,
  decodedFrames: 0,
  errorFrames: 0,
  lastFrameTimestamp: 0,
  lastFrameLength: 0,
  lastCommand: null,
  lastAddress: null,
  lastError: null,
  lastErrorTimestamp: 0,
  trafficHistory: [],
  commandStats: new Map(),
  errorStats: new Map(),
  lengthBuckets: new Map(),
  events: [],
};

function randomByte() {
  return Math.floor(Math.random() * 256);
}

function choose(array) {
  return array[Math.floor(Math.random() * array.length)];
}

function bucketLength(length) {
  if (length <= 8) {
    return `${length}`;
  }
  const bucket = Math.ceil(length / 4) * 4;
  return `${bucket - 3}+`;
}

function addHistoryPoint(timestamp, length, isError) {
  const bucketTs = Math.floor(timestamp / 1000) * 1000;
  const history = state.trafficHistory;
  let point = history[history.length - 1];

  if (!point || point.timestamp_ms !== bucketTs) {
    point = { timestamp_ms: bucketTs, frames: 0, bytes: 0, errors: 0 };
    history.push(point);
  }

  point.frames += 1;
  point.bytes += length;
  if (isError) {
    point.errors += 1;
  }

  if (history.length > MAX_HISTORY_POINTS) {
    history.shift();
  }
}

function recordCommand(command, timestamp) {
  const entry = state.commandStats.get(command) || { count: 0, last_seen_ms: 0 };
  entry.count += 1;
  entry.last_seen_ms = timestamp;
  state.commandStats.set(command, entry);
}

function recordError(type, message, timestamp) {
  const entry = state.errorStats.get(type) || { count: 0, last_seen_ms: 0 };
  entry.count += 1;
  entry.last_seen_ms = timestamp;
  state.errorStats.set(type, entry);
  pushEvent(message, 'error', timestamp);
}

function updateLengthBuckets(length) {
  const label = bucketLength(length);
  state.lengthBuckets.set(label, (state.lengthBuckets.get(label) || 0) + 1);
}

function pushEvent(message, type, timestamp) {
  state.events.push({ message, type, timestamp_ms: timestamp });
  if (state.events.length > MAX_EVENTS) {
    state.events.shift();
  }
}

function generateFrame() {
  const timestamp = Date.now();
  const length = 4 + Math.floor(Math.random() * 20);
  const raw = Array.from({ length }, randomByte);
  const command = choose(COMMANDS);
  const address = Math.floor(Math.random() * 64);
  const isError = Math.random() < 0.08;

  state.totalFrames += 1;
  state.totalBytes += length;
  state.lastFrameTimestamp = timestamp;
  state.lastFrameLength = length;
  state.lastCommand = command;
  state.lastAddress = address;

  updateLengthBuckets(length);
  addHistoryPoint(timestamp, length, isError);

  let status = 'ok';
  let errorType = null;
  if (isError) {
    status = 'error';
    errorType = choose(ERROR_TYPES);
    state.errorFrames += 1;
    state.lastError = `Trame ${command} en erreur (${errorType})`;
    state.lastErrorTimestamp = timestamp;
    recordError(errorType, state.lastError, timestamp);
  } else {
    state.decodedFrames += 1;
    recordCommand(command, timestamp);
    if (Math.random() < 0.12) {
      pushEvent(`Commande ${command} active`, 'info', timestamp);
    }
  }

  const rawFrame = {
    type: 'uart_raw',
    timestamp_ms: timestamp,
    length,
    bytes: raw,
  };

  const decodedFrame = {
    type: 'uart_decoded',
    timestamp_ms: timestamp,
    command,
    address,
    length,
    status,
  };

  if (status === 'error') {
    decodedFrame.error_type = errorType;
    decodedFrame.message = state.lastError;
  } else {
    decodedFrame.response = 'ACK';
  }

  return { raw: rawFrame, decoded: decodedFrame };
}

function buildCommandStats() {
  return Array.from(state.commandStats.entries()).map(([command, info]) => ({
    command,
    count: info.count,
    last_seen_ms: info.last_seen_ms,
  }));
}

function buildErrorStats() {
  return Array.from(state.errorStats.entries()).map(([type, info]) => ({
    type,
    count: info.count,
    last_seen_ms: info.last_seen_ms,
  }));
}

function buildLengthDistribution() {
  return Array.from(state.lengthBuckets.entries())
    .map(([label, count]) => ({
      label,
      count,
    }))
    .sort((a, b) => {
      const lengthA = parseInt(a.label, 10);
      const lengthB = parseInt(b.label, 10);
      if (Number.isNaN(lengthA) || Number.isNaN(lengthB)) {
        return b.count - a.count;
      }
      return lengthA - lengthB;
    });
}

function getStatus() {
  return {
    connected: state.connected,
    uptime_ms: Date.now() - state.startedAt,
    frame_counts: {
      total: state.totalFrames,
      decoded: state.decodedFrames,
      errors: state.errorFrames,
    },
    byte_counts: {
      total: state.totalBytes,
    },
    last_frame_timestamp_ms: state.lastFrameTimestamp,
    last_frame_length: state.lastFrameLength,
    last_command: state.lastCommand,
    last_address: state.lastAddress,
    last_error: state.lastError,
    last_error_timestamp_ms: state.lastErrorTimestamp,
    traffic_history: state.trafficHistory.slice(-MAX_HISTORY_POINTS),
    command_stats: buildCommandStats(),
    error_breakdown: buildErrorStats(),
    length_distribution: buildLengthDistribution(),
    events: state.events.slice(-MAX_EVENTS),
  };
}

module.exports = {
  generateFrame,
  getStatus,
};
