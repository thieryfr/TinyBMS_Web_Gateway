import { initChart } from './base.js';

const MAX_LENGTH_BUCKETS = 15;

function sanitizeLength(value) {
  const number = Number.parseInt(String(value ?? 0), 10);
  return Number.isFinite(number) && number >= 0 ? number : 0;
}

function buildLengthBuckets(frames = []) {
  const counts = new Map();
  frames.forEach((frame) => {
    const length = sanitizeLength(frame.length);
    const key = length <= 8 ? length : Math.ceil(length / 4) * 4;
    const label = key <= 8 ? `${key}` : `${key - 3}+`;
    counts.set(label, (counts.get(label) || 0) + 1);
  });

  return Array.from(counts.entries())
    .sort((a, b) => {
      const lengthA = Number.parseInt(a[0], 10);
      const lengthB = Number.parseInt(b[0], 10);
      if (Number.isNaN(lengthA) || Number.isNaN(lengthB)) {
        return b[1] - a[1];
      }
      return lengthA - lengthB;
    })
    .slice(0, MAX_LENGTH_BUCKETS);
}

function buildBucketsFromDistribution(distribution = []) {
  return distribution
    .map((item) => {
      if (!item) return null;
      const label = item.label ?? item.length ?? item.bucket;
      const count = Number(item.count ?? item.value ?? 0);
      if (label === undefined || !Number.isFinite(count)) {
        return null;
      }
      return [String(label), Math.max(0, count)];
    })
    .filter(Boolean)
    .slice(0, MAX_LENGTH_BUCKETS);
}

export class UartCharts {
  constructor({ distributionElement, emptyTitle = 'En attente de trames UART…' } = {}) {
    this.distribution = distributionElement
      ? initChart(
          distributionElement,
          {
            title: {
              show: true,
              text: emptyTitle,
              left: 'center',
              top: 'middle',
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'shadow' },
              formatter: (params) => {
                const item = Array.isArray(params) ? params[0] : params;
                if (!item) {
                  return 'Pas de données';
                }
                const label = item.name;
                const count = Number(item.value ?? 0);
                return `${count} trame${count > 1 ? 's' : ''} de ${label} octets`;
              },
            },
            grid: {
              left: 48,
              right: 24,
              top: 48,
              bottom: 48,
            },
            xAxis: {
              type: 'category',
              name: 'Octets',
              nameLocation: 'middle',
              nameGap: 32,
              data: [],
            },
            yAxis: {
              type: 'value',
              name: 'Nombre de trames',
              minInterval: 1,
            },
            series: [
              {
                type: 'bar',
                name: 'Occurrences',
                barWidth: '55%',
                itemStyle: {
                  borderRadius: [10, 10, 0, 0],
                  color: 'rgba(0, 168, 150, 0.75)',
                },
                emphasis: {
                  itemStyle: {
                    color: 'rgba(255, 209, 102, 0.85)',
                  },
                },
                data: [],
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;

    this.emptyTitle = emptyTitle;
  }

  update({ rawFrames = [], lengthDistribution = [] } = {}) {
    if (!this.distribution) {
      return;
    }

    let buckets = [];
    if (Array.isArray(lengthDistribution) && lengthDistribution.length > 0) {
      buckets = buildBucketsFromDistribution(lengthDistribution);
    } else if (Array.isArray(rawFrames) && rawFrames.length > 0) {
      buckets = buildLengthBuckets(rawFrames);
    }

    if (!buckets || buckets.length === 0) {
      this.distribution.chart.setOption({
        title: { show: true, text: this.emptyTitle },
        xAxis: { data: [] },
        series: [{ data: [] }],
      });
      return;
    }

    const labels = buckets.map(([label]) => label);
    const values = buckets.map(([, count]) => count);

    this.distribution.chart.setOption({
      title: { show: false },
      xAxis: { data: labels },
      series: [{ data: values }],
    });
  }
}

export class UartTrafficChart {
  constructor(domElement, { maxPoints = 120 } = {}) {
    if (!domElement) {
      this.chart = null;
      return;
    }

    const options = {
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'cross' },
      },
      legend: {
        data: ['Trames', 'Octets'],
        top: 0,
      },
      grid: {
        top: 40,
        left: 60,
        right: 60,
        bottom: 40,
      },
      xAxis: {
        type: 'time',
        boundaryGap: false,
        axisLabel: { formatter: '{HH}:{mm}:{ss}' },
      },
      yAxis: [
        {
          type: 'value',
          name: 'Trames/s',
          minInterval: 1,
        },
        {
          type: 'value',
          name: 'Octets/s',
          position: 'right',
          axisLabel: {
            formatter: (value) => (value >= 1024 ? `${(value / 1024).toFixed(1)} kB` : value),
          },
        },
      ],
      series: [
        {
          name: 'Trames',
          type: 'line',
          smooth: true,
          showSymbol: false,
          lineStyle: { width: 2 },
          areaStyle: { opacity: 0.12 },
          data: [],
        },
        {
          name: 'Octets',
          type: 'line',
          smooth: true,
          showSymbol: false,
          lineStyle: { width: 2 },
          yAxisIndex: 1,
          areaStyle: { opacity: 0.08 },
          data: [],
        },
      ],
    };

    const { chart } = initChart(domElement, options, { renderer: 'canvas' });
    this.chart = chart;
    this.framesData = [];
    this.bytesData = [];
    this.maxPoints = maxPoints;
  }

  setData(history = []) {
    if (!this.chart) {
      return;
    }

    this.framesData = [];
    this.bytesData = [];

    history.forEach((point) => {
      if (!point) return;
      const timestamp = Number(point.timestamp_ms ?? point.timestamp ?? Date.now());
      const frames = Number(point.frames ?? point.count ?? 0) || 0;
      const bytes = Number(point.bytes ?? point.octets ?? 0) || 0;

      const date = new Date(timestamp);
      this.framesData.push([date, frames]);
      this.bytesData.push([date, bytes]);
    });

    if (this.framesData.length > this.maxPoints) {
      this.framesData = this.framesData.slice(-this.maxPoints);
    }
    if (this.bytesData.length > this.maxPoints) {
      this.bytesData = this.bytesData.slice(-this.maxPoints);
    }

    this.chart.setOption({
      series: [
        { data: this.framesData },
        { data: this.bytesData },
      ],
      xAxis: {
        min: this.framesData.length ? 'dataMin' : null,
        max: this.framesData.length ? 'dataMax' : null,
      },
    });
  }

  addPoint({ timestamp_ms, frames = 0, bytes = 0 } = {}) {
    if (!this.chart) {
      return;
    }

    const date = new Date(Number(timestamp_ms) || Date.now());
    const frameValue = Number(frames) || 0;
    const byteValue = Number(bytes) || 0;

    this.framesData.push([date, frameValue]);
    this.bytesData.push([date, byteValue]);

    if (this.framesData.length > this.maxPoints) {
      this.framesData.shift();
    }
    if (this.bytesData.length > this.maxPoints) {
      this.bytesData.shift();
    }

    this.chart.setOption({
      series: [
        { data: this.framesData },
        { data: this.bytesData },
      ],
    });
  }

  clear() {
    if (!this.chart) {
      return;
    }
    this.framesData = [];
    this.bytesData = [];
    this.chart.setOption({
      series: [
        { data: [] },
        { data: [] },
      ],
    });
  }
}

export class UartCommandDistributionChart {
  constructor(domElement) {
    if (!domElement) {
      this.chart = null;
      return;
    }

    const options = {
      tooltip: {
        trigger: 'item',
        formatter: '{b}: {c} ({d}%)',
      },
      legend: {
        orient: 'vertical',
        left: 'left',
        top: 'middle',
      },
      series: [
        {
          name: 'Commandes',
          type: 'pie',
          radius: ['45%', '70%'],
          avoidLabelOverlap: true,
          itemStyle: {
            borderRadius: 8,
            borderColor: '#141b26',
            borderWidth: 2,
          },
          label: {
            show: true,
            formatter: '{b}\n{d}%',
          },
          emphasis: {
            label: {
              show: true,
              fontSize: 16,
              fontWeight: 'bold',
            },
          },
          data: [],
        },
      ],
    };

    const { chart } = initChart(domElement, options, { renderer: 'canvas' });
    this.chart = chart;
    this.seriesData = [];
  }

  setData(commands = []) {
    if (!this.chart) {
      return;
    }

    this.seriesData = commands
      .map((item) => {
        if (!item) return null;
        const name = item.command ?? item.name ?? item.id;
        const value = Number(item.count ?? item.value ?? 0);
        if (!name || !Number.isFinite(value) || value <= 0) {
          return null;
        }
        return { name: String(name).toUpperCase(), value };
      })
      .filter(Boolean);

    this.chart.setOption({
      series: [
        {
          data: this.seriesData,
        },
      ],
    });
  }

  increment(command) {
    if (!this.chart || !command) {
      return;
    }

    const normalized = String(command).toUpperCase();
    const existing = this.seriesData.find((item) => item.name === normalized);

    if (existing) {
      existing.value += 1;
    } else {
      this.seriesData.push({ name: normalized, value: 1 });
    }

    this.chart.setOption({
      series: [
        {
          data: this.seriesData.slice(),
        },
      ],
    });
  }

  clear() {
    if (!this.chart) {
      return;
    }
    this.seriesData = [];
    this.chart.setOption({
      series: [
        {
          data: [],
        },
      ],
    });
  }
}
