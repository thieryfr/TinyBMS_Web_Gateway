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

export class UartCharts {
  constructor({ distributionElement } = {}) {
    this.distribution = distributionElement
      ? initChart(
          distributionElement,
          {
            title: {
              show: true,
              text: 'En attente de trames UART…',
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
  }

  update({ rawFrames = [] } = {}) {
    if (!this.distribution) {
      return;
    }

    if (!Array.isArray(rawFrames) || rawFrames.length === 0) {
      this.distribution.chart.setOption({
        title: { show: true },
        xAxis: { data: [] },
        series: [{ data: [] }],
      });
      return;
    }

    const buckets = buildLengthBuckets(rawFrames);
    const labels = buckets.map(([label]) => label);
    const values = buckets.map(([, count]) => count);

    this.distribution.chart.setOption({
      title: { show: false },
      xAxis: { data: labels },
      series: [{ data: values }],
    });
  }
}
