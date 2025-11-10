import { initChart } from './base.js';

const DEFAULT_BUCKET_COUNT = 12;
const MAX_ID_ROWS = 8;

function formatCanId(value) {
  if (typeof value === 'number' && Number.isFinite(value)) {
    const hex = value.toString(16).toUpperCase();
    const width = value <= 0x7FF ? 3 : hex.length;
    return `0x${hex.padStart(width, '0')}`;
  }
  if (typeof value === 'string') {
    const trimmed = value.trim();
    if (trimmed.length === 0) {
      return '—';
    }
    if (/^0x/i.test(trimmed)) {
      const parsed = Number.parseInt(trimmed, 16);
      if (Number.isFinite(parsed)) {
        const hex = parsed.toString(16).toUpperCase();
        const width = parsed <= 0x7FF ? 3 : hex.length;
        return `0x${hex.padStart(width, '0')}`;
      }
      return trimmed.toUpperCase();
    }
    const parsedDec = Number.parseInt(trimmed, 10);
    if (Number.isFinite(parsedDec)) {
      const hex = parsedDec.toString(16).toUpperCase();
      const width = parsedDec <= 0x7FF ? 3 : hex.length;
      return `0x${hex.padStart(width, '0')}`;
    }
    return trimmed.toUpperCase();
  }
  return '—';
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function resolveFrameTimestamp(frame) {
  if (!frame || typeof frame !== 'object') {
    return Number.NaN;
  }

  const timestampMs = Number(frame.timestamp_ms ?? frame.timestampMs ?? frame.time_ms);
  if (Number.isFinite(timestampMs)) {
    return timestampMs;
  }

  const timestamp = Number(frame.timestamp ?? frame.time);
  if (Number.isFinite(timestamp)) {
    return timestamp;
  }

  if (typeof frame.timestamp_iso === 'string' && frame.timestamp_iso) {
    const parsed = Date.parse(frame.timestamp_iso);
    if (!Number.isNaN(parsed)) {
      return parsed;
    }
  }

  return Number.NaN;
}

function resolveFrameLength(frame) {
  if (!frame || typeof frame !== 'object') {
    return 0;
  }

  const direct = Number(frame.length ?? frame.dlc ?? frame.payload_length);
  if (Number.isFinite(direct) && direct >= 0) {
    return direct;
  }

  if (Array.isArray(frame.data)) {
    return frame.data.length;
  }

  if (Array.isArray(frame.bytes)) {
    return frame.bytes.length;
  }

  const payload = frame.data ?? frame.payload ?? frame.raw;
  if (typeof payload === 'string') {
    const sanitized = payload.trim().replace(/^0x/i, '').replace(/\s+/g, '');
    if (sanitized.length === 0) {
      return 0;
    }
    return Math.max(0, Math.floor(sanitized.length / 2));
  }

  return 0;
}

export function estimateCanBusOccupancy(frames, { bitrate = 500000, windowSeconds = 60 } = {}) {
  const safeBitrate = Number.isFinite(bitrate) && bitrate > 0 ? bitrate : 500000;
  const safeWindow = Number.isFinite(windowSeconds) && windowSeconds > 0 ? windowSeconds : 60;

  if (!Array.isArray(frames) || frames.length === 0) {
    return {
      occupancyPct: 0,
      sampleCount: 0,
      windowMs: safeWindow * 1000,
      bitrate: safeBitrate,
      totalBits: 0,
    };
  }

  const now = Date.now();
  const cutoff = now - safeWindow * 1000;
  let totalBits = 0;
  let sampleCount = 0;

  frames.forEach((frame) => {
    const timestamp = resolveFrameTimestamp(frame);
    if (!Number.isFinite(timestamp) || timestamp < cutoff) {
      return;
    }

    const bitLength = Number(frame.bit_length ?? frame.bitLength);
    if (Number.isFinite(bitLength) && bitLength > 0) {
      totalBits += bitLength;
    } else {
      const payloadBytes = resolveFrameLength(frame);
      const estimatedBits = 47 + Math.max(0, payloadBytes) * 8;
      totalBits += estimatedBits;
    }
    sampleCount += 1;
  });

  const capacity = safeBitrate * safeWindow;
  const rawOccupancy = capacity > 0 ? (totalBits / capacity) * 100 : 0;
  const occupancyPct = clamp(Number.isFinite(rawOccupancy) ? Number(rawOccupancy.toFixed(2)) : 0, 0, 100);

  return {
    occupancyPct,
    sampleCount,
    windowMs: safeWindow * 1000,
    bitrate: safeBitrate,
    totalBits,
  };
}

function buildBucketContext(windowSeconds) {
  const safeWindow = Number.isFinite(windowSeconds) && windowSeconds > 0 ? windowSeconds : 300;
  const bucketCount = Math.max(4, Math.min(DEFAULT_BUCKET_COUNT, safeWindow));
  const bucketDuration = Math.max(1, Math.floor(safeWindow / bucketCount));
  return { bucketCount, bucketDuration, windowSeconds: safeWindow };
}

function buildBucketLabels({ bucketCount, bucketDuration, windowSeconds }, now) {
  const start = now - windowSeconds * 1000;
  const labels = [];
  for (let index = 0; index < bucketCount; index += 1) {
    const bucketEnd = start + (index + 1) * bucketDuration * 1000;
    const date = new Date(bucketEnd);
    labels.push(
      date.toLocaleTimeString([], {
        hour12: false,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      })
    );
  }
  return { labels, start };
}

function groupFramesById(frames, context, start) {
  const map = new Map();
  frames.forEach((frame) => {
    if (!frame || !Number.isFinite(frame.timestamp)) {
      return;
    }
    const offset = frame.timestamp - start;
    if (offset < 0) {
      return;
    }
    const bucket = clamp(Math.floor(offset / (context.bucketDuration * 1000)), 0, context.bucketCount - 1);
    const idLabel = formatCanId(frame.id ?? frame.identifier);
    if (!map.has(idLabel)) {
      map.set(idLabel, new Array(context.bucketCount).fill(0));
    }
    const bucketCounts = map.get(idLabel);
    bucketCounts[bucket] += 1;
  });
  return map;
}

function selectTopIdentifiers(map) {
  return Array.from(map.entries())
    .map(([id, counts]) => ({ id, counts, total: counts.reduce((sum, value) => sum + value, 0) }))
    .filter((entry) => entry.total > 0)
    .sort((a, b) => b.total - a.total)
    .slice(0, MAX_ID_ROWS);
}

export class CanCharts {
  constructor({ heatmapElement, throughputElement } = {}) {
    this.heatmap = heatmapElement
      ? initChart(
          heatmapElement,
          {
            title: {
              show: true,
              text: 'En attente de trames CAN…',
              left: 'center',
              top: 'middle',
            },
            grid: {
              left: 80,
              right: 24,
              top: 40,
              bottom: 48,
            },
            xAxis: {
              type: 'category',
              data: [],
              boundaryGap: true,
            },
            yAxis: {
              type: 'category',
              data: [],
              inverse: true,
            },
            visualMap: {
              show: false,
              min: 0,
              max: 10,
              calculable: false,
            },
            tooltip: {
              trigger: 'item',
              formatter: (params) => {
                if (!params) {
                  return 'Pas de données';
                }
                const value = Number(params.value?.[2] ?? 0);
                if (!Number.isFinite(value) || value <= 0) {
                  return `${params.name} • ${params.value?.[1] ?? ''}: aucune trame`;
                }
                const idLabel = params.value?.[1] ?? params.name;
                return `${idLabel}<br/>Fenêtre ${params.value?.[0] ?? params.name}: ${value} trame${value > 1 ? 's' : ''}`;
              },
            },
            series: [
              {
                type: 'heatmap',
                data: [],
                label: { show: false },
                emphasis: {
                  itemStyle: {
                    shadowBlur: 18,
                    shadowColor: 'rgba(0, 0, 0, 0.35)',
                  },
                },
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;

    this.throughput = throughputElement
      ? initChart(
          throughputElement,
          {
            title: {
              show: true,
              text: 'Aucune trame sur la période sélectionnée',
              left: 'center',
              top: 'middle',
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'shadow' },
              valueFormatter: (value) => {
                const number = Number(value);
                if (!Number.isFinite(number)) {
                  return '-- tr/s';
                }
                return `${number.toFixed(2)} tr/s`;
              },
            },
            grid: {
              left: 60,
              right: 24,
              top: 48,
              bottom: 48,
            },
            xAxis: {
              type: 'category',
              data: [],
            },
            yAxis: {
              type: 'value',
              name: 'Trames par seconde',
            },
            series: [
              {
                type: 'bar',
                name: 'Débit',
                barWidth: '55%',
                itemStyle: {
                  borderRadius: [8, 8, 0, 0],
                  color: 'rgba(255, 209, 102, 0.75)',
                },
                emphasis: {
                  itemStyle: {
                    color: 'rgba(241, 91, 181, 0.85)',
                  },
                },
                data: [],
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;
  }

  update({ rawFrames = [], decodedFrames = [], filters = {} } = {}) {
    const { source = 'all', windowSeconds = 300 } = filters || {};
    if (!this.heatmap && !this.throughput) {
      return;
    }

    const now = Date.now();
    const context = buildBucketContext(windowSeconds);
    const { labels: bucketLabels, start } = buildBucketLabels(context, now);
    const cutoff = start;

    let framesToUse = [];
    if (source === 'raw') {
      framesToUse = rawFrames;
    } else if (source === 'decoded') {
      framesToUse = decodedFrames;
    } else {
      framesToUse = [...rawFrames, ...decodedFrames];
    }

    const filteredFrames = framesToUse.filter((frame) => Number.isFinite(frame?.timestamp) && frame.timestamp >= cutoff);

    this.updateHeatmap(filteredFrames, { context, bucketLabels, start });
    this.updateThroughput(filteredFrames, { context, bucketLabels, start });
  }

  updateHeatmap(frames, { context, bucketLabels, start }) {
    if (!this.heatmap) {
      return;
    }

    if (!Array.isArray(frames) || frames.length === 0) {
      this.heatmap.chart.setOption({
        title: { show: true },
        xAxis: { data: bucketLabels },
        yAxis: { data: [] },
        series: [{ data: [] }],
      });
      return;
    }

    const grouped = groupFramesById(frames, context, start);
    const topIds = selectTopIdentifiers(grouped);
    const yAxis = topIds.map((entry) => entry.id);
    const data = [];
    let maxValue = 0;

    topIds.forEach((entry, rowIndex) => {
      entry.counts.forEach((value, columnIndex) => {
        if (value > 0) {
          data.push([columnIndex, rowIndex, value]);
          maxValue = Math.max(maxValue, value);
        }
      });
    });

    this.heatmap.chart.setOption({
      title: { show: false },
      xAxis: { data: bucketLabels },
      yAxis: { data: yAxis },
      visualMap: { max: Math.max(1, maxValue) },
      series: [{ data }],
    });
  }

  updateThroughput(frames, { context, bucketLabels, start }) {
    if (!this.throughput) {
      return;
    }

    if (!Array.isArray(frames) || frames.length === 0) {
      this.throughput.chart.setOption({
        title: { show: true },
        xAxis: { data: bucketLabels },
        series: [{ data: [] }],
      });
      return;
    }

    const counts = new Array(context.bucketCount).fill(0);
    frames.forEach((frame) => {
      if (!frame || !Number.isFinite(frame.timestamp)) {
        return;
      }
      const offset = frame.timestamp - start;
      if (offset < 0) {
        return;
      }
      const bucket = clamp(Math.floor(offset / (context.bucketDuration * 1000)), 0, context.bucketCount - 1);
      counts[bucket] += 1;
    });

    const rates = counts.map((count) => count / context.bucketDuration);

    this.throughput.chart.setOption({
      title: { show: false },
      xAxis: { data: bucketLabels },
      series: [{ data: rates }],
    });
  }
}
