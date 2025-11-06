import { initChart } from './base.js';

const ENERGY_HISTORY_LIMIT = 30; // Keep last 30 samples

function sanitizeNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

export class EnergyCharts {
  constructor({ energyBarChartElement } = {}) {
    this.energySamples = [];
    this.lastEnergyIn = 0;
    this.lastEnergyOut = 0;

    this.energyBarChart = energyBarChartElement
      ? initChart(
          energyBarChartElement,
          {
            title: {
              text: 'Énergie IN/OUT',
              left: 'center',
              top: 10,
              textStyle: {
                color: 'rgba(255,255,255,0.85)',
                fontSize: 16,
                fontWeight: 600,
              },
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: {
                type: 'shadow',
              },
              formatter: (params) => {
                if (!params || params.length === 0) {
                  return 'Pas de données';
                }

                const timeLabel = params[0].axisValueLabel || '';
                let html = `<div style="font-weight: 600; margin-bottom: 8px;">${timeLabel}</div>`;

                params.forEach((param) => {
                  const value = Math.abs(param.value);
                  const color = param.seriesName === 'IN' ? '#00a896' : '#ff9800';
                  const arrow = param.seriesName === 'IN' ? '↓' : '↑';
                  html += `<div style="display: flex; align-items: center; margin-bottom: 4px;">
                    <span style="display: inline-block; width: 10px; height: 10px; background-color: ${color}; border-radius: 50%; margin-right: 8px;"></span>
                    <span style="flex: 1;">${arrow} Énergie ${param.seriesName}</span>
                    <span style="font-weight: 600; margin-left: 12px;">${value.toFixed(2)} Wh</span>
                  </div>`;
                });

                return html;
              },
            },
            legend: {
              data: ['IN', 'OUT'],
              top: 40,
              textStyle: {
                color: 'rgba(255,255,255,0.75)',
                fontSize: 12,
              },
              itemWidth: 14,
              itemHeight: 14,
            },
            grid: {
              left: 60,
              right: 40,
              top: 80,
              bottom: 40,
              containLabel: true,
            },
            xAxis: {
              type: 'category',
              data: [],
              axisLabel: {
                color: 'rgba(255,255,255,0.65)',
                fontSize: 11,
                interval: 'auto',
                rotate: 45,
              },
              axisLine: {
                lineStyle: {
                  color: 'rgba(255,255,255,0.25)',
                },
              },
              axisTick: {
                show: false,
              },
            },
            yAxis: {
              type: 'value',
              axisLabel: {
                color: 'rgba(255,255,255,0.65)',
                fontSize: 11,
                formatter: (value) => {
                  return value >= 0 ? `+${value} Wh` : `${value} Wh`;
                },
              },
              axisLine: {
                show: false,
              },
              splitLine: {
                lineStyle: {
                  color: 'rgba(255,255,255,0.1)',
                },
              },
            },
            series: [
              {
                name: 'IN',
                type: 'bar',
                stack: 'energy',
                data: [],
                itemStyle: {
                  color: '#00a896',
                  borderRadius: [5, 5, 0, 0],
                },
                emphasis: {
                  focus: 'series',
                },
              },
              {
                name: 'OUT',
                type: 'bar',
                stack: 'energy',
                data: [],
                itemStyle: {
                  color: '#ff9800',
                  borderRadius: [0, 0, 5, 5],
                },
                emphasis: {
                  focus: 'series',
                },
              },
            ],
          },
          { renderer: 'canvas' }
        )
      : null;
  }

  update({ energyChargedWh, energyDischargedWh } = {}) {
    if (!this.energyBarChart) {
      return;
    }

    const energyIn = sanitizeNumber(energyChargedWh);
    const energyOut = sanitizeNumber(energyDischargedWh);

    if (energyIn == null || energyOut == null) {
      return;
    }

    // Calculate delta (difference from last update)
    const deltaIn = energyIn - this.lastEnergyIn;
    const deltaOut = energyOut - this.lastEnergyOut;

    // Update last values
    this.lastEnergyIn = energyIn;
    this.lastEnergyOut = energyOut;

    // Only add to chart if there was a change
    if (deltaIn === 0 && deltaOut === 0) {
      return;
    }

    const timestamp = new Date();
    const label = timestamp.toLocaleTimeString([], {
      hour12: false,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });

    // Add new sample (IN is positive, OUT is negative)
    this.energySamples.push({
      label,
      energyIn: Math.max(0, deltaIn),  // Positive values
      energyOut: -Math.max(0, deltaOut) // Negative values
    });

    // Keep only last N samples
    if (this.energySamples.length > ENERGY_HISTORY_LIMIT) {
      this.energySamples.splice(0, this.energySamples.length - ENERGY_HISTORY_LIMIT);
    }

    // Update chart
    const labels = this.energySamples.map(s => s.label);
    const dataIn = this.energySamples.map(s => s.energyIn);
    const dataOut = this.energySamples.map(s => s.energyOut);

    this.energyBarChart.chart.setOption({
      xAxis: {
        data: labels,
      },
      series: [
        {
          data: dataIn,
        },
        {
          data: dataOut,
        },
      ],
    });
  }

  reset() {
    this.energySamples = [];
    this.lastEnergyIn = 0;
    this.lastEnergyOut = 0;

    if (this.energyBarChart) {
      this.energyBarChart.chart.setOption({
        xAxis: { data: [] },
        series: [
          { data: [] },
          { data: [] },
        ],
      });
    }
  }
}
