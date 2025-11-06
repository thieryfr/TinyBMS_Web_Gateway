import { initChart } from './base.js';

const DEFAULT_SPARKLINE_LIMIT = 60;

function sanitizeNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

export class BatteryRealtimeCharts {
  constructor({ gaugeElement, voltageSparklineElement, currentSparklineElement, cellChartElement, temperatureGaugeElement } = {}) {
    this.sparklineLimit = DEFAULT_SPARKLINE_LIMIT;
    this.voltageSamples = [];
    this.currentSamples = [];
    this.cellVoltages = [];

    this.gauge = gaugeElement
      ? initChart(
          gaugeElement,
          {
            tooltip: {
              formatter: ({ seriesName, value }) =>
                value != null ? `${seriesName}: ${value.toFixed(1)} %` : `${seriesName} indisponible`,
            },
            series: [
              // SOC Needle (primary - with dial)
              {
                name: 'SOC',
                type: 'gauge',
                startAngle: 220,
                endAngle: -40,
                min: 0,
                max: 100,
                splitNumber: 5,
                center: ['50%', '60%'],
                radius: '95%',
                pointer: {
                  icon: 'path://M12 4L8 12H16L12 4Z',
                  length: '65%',
                  width: 5,
                  itemStyle: {
                    color: '#00a896',
                  },
                },
                axisLine: {
                  lineStyle: {
                    width: 10,
                    color: [
                      [0.5, '#f25f5c'],
                      [0.8, '#ffd166'],
                      [1, '#00a896'],
                    ],
                  },
                },
                axisTick: {
                  distance: 2,
                  length: 5,
                  lineStyle: { color: 'rgba(255,255,255,0.35)', width: 1 },
                },
                splitLine: {
                  length: 8,
                  lineStyle: { color: 'rgba(255,255,255,0.45)', width: 2 },
                },
                axisLabel: {
                  color: 'rgba(255,255,255,0.7)',
                  distance: 10,
                  fontSize: 10,
                },
                detail: {
                  valueAnimation: true,
                  fontSize: 12,
                  fontWeight: 600,
                  offsetCenter: [0, '50%'],
                  color: '#00a896',
                  formatter: (value) =>
                    value != null ? `${value.toFixed(1)}%` : '-- %',
                },
                anchor: {
                  show: true,
                  showAbove: true,
                  size: 8,
                  itemStyle: {
                    color: '#00a896',
                  },
                },
                title: {
                  show: false,
                },
                data: [
                  {
                    value: 0,
                    name: 'SOC',
                  },
                ],
              },
              // SOH Needle (secondary - needle only, no dial)
              {
                name: 'SOH',
                type: 'gauge',
                startAngle: 220,
                endAngle: -40,
                min: 0,
                max: 100,
                splitNumber: 5,
                center: ['50%', '60%'],
                radius: '95%',
                pointer: {
                  icon: 'path://M12 4L8 12H16L12 4Z',
                  length: '50%',
                  width: 4,
                  itemStyle: {
                    color: '#ffd166',
                  },
                },
                axisLine: {
                  show: false,
                },
                axisTick: {
                  show: false,
                },
                splitLine: {
                  show: false,
                },
                axisLabel: {
                  show: false,
                },
                detail: {
                  valueAnimation: true,
                  fontSize: 12,
                  fontWeight: 600,
                  offsetCenter: [0, '70%'],
                  color: '#ffd166',
                  formatter: (value) =>
                    value != null ? `${value.toFixed(1)}%` : '-- %',
                },
                anchor: {
                  show: true,
                  showAbove: false,
                  size: 6,
                  itemStyle: {
                    color: '#ffd166',
                  },
                },
                title: {
                  show: false,
                },
                data: [
                  {
                    value: 0,
                    name: 'SOH',
                  },
                ],
              },
            ],
          },
          { renderer: 'svg' }
        )
      : null;

    this.voltageSparkline = voltageSparklineElement
      ? initChart(
          voltageSparklineElement,
          {
            grid: {
              left: 4,
              right: 4,
              top: 12,
              bottom: 8,
              containLabel: false,
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'line' },
              valueFormatter: (value) =>
                value != null ? `${value.toFixed(2)} V` : '--',
              formatter: (params) => {
                if (!params || params.length === 0) {
                  return 'Pas de données';
                }
                const item = params[0];
                const val = Number.isFinite(item.data) ? item.data.toFixed(2) : '--';
                const timeLabel = item.axisValueLabel || '';
                return `${timeLabel}<br/>Tension: ${val} V`;
              },
            },
            legend: {
              top: 0,
              textStyle: { color: 'rgba(255,255,255,0.65)', fontSize: 12 },
              itemWidth: 12,
              itemHeight: 12,
            },
            xAxis: {
              type: 'category',
              boundaryGap: false,
              axisLine: { show: false },
              axisTick: { show: false },
              axisLabel: { show: false },
              data: [],
            },
            yAxis: {
              type: 'value',
              show: false,
              min: (value) => value.min,
              max: (value) => value.max,
            },
            series: [
              {
                name: 'Tension',
                type: 'line',
                smooth: true,
                symbol: 'none',
                lineStyle: { width: 2 },
                data: [],
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;

    this.currentSparkline = currentSparklineElement
      ? initChart(
          currentSparklineElement,
          {
            grid: {
              left: 4,
              right: 4,
              top: 12,
              bottom: 8,
              containLabel: false,
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'line' },
              valueFormatter: (value) =>
                value != null ? `${value.toFixed(2)} A` : '--',
              formatter: (params) => {
                if (!params || params.length === 0) {
                  return 'Pas de données';
                }
                const item = params[0];
                const val = Number.isFinite(item.data) ? item.data.toFixed(2) : '--';
                const timeLabel = item.axisValueLabel || '';
                return `${timeLabel}<br/>Courant: ${val} A`;
              },
            },
            legend: {
              top: 0,
              textStyle: { color: 'rgba(255,255,255,0.65)', fontSize: 12 },
              itemWidth: 12,
              itemHeight: 12,
            },
            xAxis: {
              type: 'category',
              boundaryGap: false,
              axisLine: { show: false },
              axisTick: { show: false },
              axisLabel: { show: false },
              data: [],
            },
            yAxis: {
              type: 'value',
              show: false,
              min: (value) => value.min,
              max: (value) => value.max,
            },
            series: [
              {
                name: 'Courant',
                type: 'line',
                smooth: true,
                symbol: 'none',
                lineStyle: { width: 2 },
                data: [],
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;

    this.cellChart = cellChartElement
      ? initChart(
          cellChartElement,
          {
            title: {
              show: false,
              text: 'Données cellules indisponibles',
              left: 'center',
              top: 'middle',
              textStyle: {
                color: 'rgba(240, 248, 255, 0.7)',
                fontSize: 16,
                fontWeight: 500,
              },
            },
            legend: {
              bottom: 0,
              textStyle: { color: 'rgba(255,255,255,0.65)' },
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'shadow' },
            },
            grid: {
              left: 60,
              right: 24,
              top: 40,
              bottom: 60,
            },
            xAxis: {
              type: 'category',
              axisLabel: { color: 'rgba(255,255,255,0.75)' },
              axisLine: { lineStyle: { color: 'rgba(255,255,255,0.25)' } },
              axisTick: { show: false },
              data: [],
            },
            yAxis: {
              type: 'value',
              axisLabel: {
                formatter: '{value}%',
                color: 'rgba(255,255,255,0.75)',
              },
              axisLine: { lineStyle: { color: 'rgba(255,255,255,0.25)' } },
              splitLine: {
                lineStyle: { color: 'rgba(255,255,255,0.1)' },
              },
              max: 100,
            },
            series: [
              {
                name: 'Tension',
                type: 'bar',
                stack: 'cells',
                percentage: true,
                emphasis: { focus: 'series' },
                itemStyle: { color: '#00a896' },
                barWidth: '60%',
                data: [],
              },
              {
                name: 'Écart',
                type: 'bar',
                stack: 'cells',
                percentage: true,
                emphasis: { focus: 'series' },
                itemStyle: { color: 'rgba(255,255,255,0.12)' },
                barWidth: '60%',
                data: [],
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;

    this.temperatureGauge = temperatureGaugeElement
      ? initChart(
          temperatureGaugeElement,
          {
            tooltip: {
              formatter: ({ value }) =>
                value != null ? `${value.toFixed(1)} °C` : 'Température indisponible',
            },
            series: [
              {
                name: 'Température',
                type: 'gauge',
                startAngle: 220,
                endAngle: -40,
                min: -20,
                max: 80,
                splitNumber: 5,
                center: ['50%', '60%'],
                radius: '95%',
                pointer: {
                  icon: 'path://M12 4L8 12H16L12 4Z',
                  length: '65%',
                  width: 5,
                },
                axisLine: {
                  lineStyle: {
                    width: 10,
                    color: [
                      [0.3, '#00a896'],
                      [0.6, '#ffd166'],
                      [1, '#f25f5c'],
                    ],
                  },
                },
                axisTick: {
                  distance: 2,
                  length: 5,
                  lineStyle: { color: 'rgba(255,255,255,0.35)', width: 1 },
                },
                splitLine: {
                  length: 8,
                  lineStyle: { color: 'rgba(255,255,255,0.45)', width: 2 },
                },
                axisLabel: {
                  color: 'rgba(255,255,255,0.7)',
                  distance: 10,
                  fontSize: 10,
                  formatter: '{value}°',
                },
                detail: {
                  valueAnimation: true,
                  fontSize: 14,
                  fontWeight: 600,
                  offsetCenter: [0, '60%'],
                  color: '#f2f5f7',
                  formatter: (value) =>
                    value != null ? `${value.toFixed(1)}°C` : '-- °C',
                },
                anchor: {
                  show: true,
                  showAbove: true,
                  size: 8,
                  itemStyle: {
                    color: '#f2f5f7',
                  },
                },
                title: {
                  show: false,
                },
                data: [
                  {
                    value: 0,
                    name: 'Température',
                  },
                ],
              },
            ],
          },
          { renderer: 'svg' }
        )
      : null;
  }

  update({ voltage, current, soc, soh, voltagesMv, balancingStates, temperature } = {}) {
    this.updateGauge(soc, soh);
    this.updateSparkline({ voltage, current });
    this.updateCellChart(voltagesMv);
    this.updateTemperatureGauge(temperature);
  }

  updateGauge(rawSoc, rawSoh) {
    if (!this.gauge) {
      return;
    }
    const socValue = sanitizeNumber(rawSoc);
    const sohValue = sanitizeNumber(rawSoh);
    this.gauge.chart.setOption({
      series: [
        // Update SOC (first series)
        {
          data: [
            {
              value: socValue == null ? null : Math.max(0, Math.min(100, socValue)),
              name: 'SOC',
            },
          ],
        },
        // Update SOH (second series)
        {
          data: [
            {
              value: sohValue == null ? null : Math.max(0, Math.min(100, sohValue)),
              name: 'SOH',
            },
          ],
        },
      ],
    });
  }

  updateTemperatureGauge(rawTemperature) {
    if (!this.temperatureGauge) {
      return;
    }
    const value = sanitizeNumber(rawTemperature);
    this.temperatureGauge.chart.setOption({
      series: [
        {
          data: [
            {
              value: value,
              name: 'Température',
            },
          ],
        },
      ],
    });
  }

  updateSparkline({ voltage, current }) {
    const voltageValue = sanitizeNumber(voltage);
    const currentValue = sanitizeNumber(current);
    const timestamp = new Date();
    const label = timestamp.toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });

    // Update voltage sparkline
    if (this.voltageSparkline) {
      this.voltageSamples.push({ label, value: voltageValue });
      if (this.voltageSamples.length > this.sparklineLimit) {
        this.voltageSamples.splice(0, this.voltageSamples.length - this.sparklineLimit);
      }

      const labels = this.voltageSamples.map((sample) => sample.label);
      const values = this.voltageSamples.map((sample) => sample.value);

      this.voltageSparkline.chart.setOption({
        xAxis: { data: labels },
        series: [{ data: values }],
      });
    }

    // Update current sparkline
    if (this.currentSparkline) {
      this.currentSamples.push({ label, value: currentValue });
      if (this.currentSamples.length > this.sparklineLimit) {
        this.currentSamples.splice(0, this.currentSamples.length - this.sparklineLimit);
      }

      const labels = this.currentSamples.map((sample) => sample.label);
      const values = this.currentSamples.map((sample) => sample.value);

      this.currentSparkline.chart.setOption({
        xAxis: { data: labels },
        series: [{ data: values }],
      });
    }
  }

  updateCellChart(voltagesMv) {
    if (!this.cellChart) {
      return;
    }

    if (!Array.isArray(voltagesMv) || voltagesMv.length === 0) {
      this.cellVoltages = [];
      this.cellChart.chart.setOption({
        title: { show: true },
        xAxis: { data: [] },
        series: [{ data: [] }, { data: [] }],
      });
      return;
    }

    const voltages = voltagesMv.map((value) => {
      const number = Number(value);
      return Number.isFinite(number) ? number / 1000 : null;
    });

    if (voltages.every((value) => value == null)) {
      this.cellVoltages = [];
      this.cellChart.chart.setOption({
        title: { show: true },
        xAxis: { data: [] },
        series: [{ data: [] }, { data: [] }],
      });
      return;
    }

    const resolvedVoltages = voltages.map((value) => (value == null ? 0 : value));
    this.cellVoltages = resolvedVoltages;
    const categories = resolvedVoltages.map((_, index) => `Cellule ${index + 1}`);
    const maxVoltage = Math.max(...resolvedVoltages, 0);
    const differenceSeries = resolvedVoltages.map((value) => Math.max(maxVoltage - value, 0));

    const tooltipFormatter = (params = []) => {
      if (!params.length) {
        return '';
      }
      const index = params[0]?.dataIndex ?? 0;
      const voltageValue = resolvedVoltages[index];
      const diff = differenceSeries[index];
      const percent = params[0]?.value != null ? Number(params[0].value).toFixed(1) : '--';
      return [
        `Cellule ${index + 1}`,
        `Tension: ${voltageValue.toFixed(3)} V`,
        `Écart: ${diff.toFixed(3)} V`,
        `Part: ${percent}%`,
      ].join('<br/>');
    };

    this.cellChart.chart.setOption({
      title: { show: false },
      tooltip: { formatter: tooltipFormatter },
      xAxis: { data: categories },
      series: [
        { data: resolvedVoltages },
        { data: differenceSeries },
      ],
    });
  }

}
