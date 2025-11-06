import { initChart } from './base.js';

const DEFAULT_SPARKLINE_LIMIT = 60;
const UNDER_VOLTAGE_CUTOFF = 2800; // mV
const OVER_VOLTAGE_CUTOFF = 3800; // mV

// Default register values for dynamic axes
const DEFAULT_OVERVOLTAGE_MV = 3800;
const DEFAULT_UNDERVOLTAGE_MV = 2800;
const DEFAULT_PEAK_DISCHARGE_A = 70;
const DEFAULT_CHARGE_OVERCURRENT_A = 90;
const NUM_CELLS = 16;

function sanitizeNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

export class BatteryRealtimeCharts {
  constructor({ gaugeElement, voltageSparklineElement, currentSparklineElement, cellChartElement, temperatureGaugeElement, remainingGaugeElement } = {}) {
    this.sparklineLimit = DEFAULT_SPARKLINE_LIMIT;
    this.voltageSamples = [];
    this.currentSamples = [];
    this.cellVoltages = [];

    // Dynamic axis limits from BMS registers
    // Use tighter ranges for better visibility of variations
    const nominal_voltage_v = ((DEFAULT_OVERVOLTAGE_MV + DEFAULT_UNDERVOLTAGE_MV) / 2 * NUM_CELLS) / 1000;
    const voltage_margin = nominal_voltage_v * 0.1; // ±10% around nominal

    this.voltageLimits = {
      min: nominal_voltage_v - voltage_margin,
      max: nominal_voltage_v + voltage_margin,
    };
    this.currentLimits = {
      min: -DEFAULT_CHARGE_OVERCURRENT_A * 0.3, // 30% of charge current
      max: DEFAULT_PEAK_DISCHARGE_A * 0.3, // 30% of peak discharge
    };

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
              show: true,
              position: 'right',
              min: this.voltageLimits.min,
              max: this.voltageLimits.max,
              axisLabel: {
                formatter: '{value} V',
                color: 'rgba(255,255,255,0.5)',
                fontSize: 10,
              },
              axisLine: { show: false },
              splitLine: {
                lineStyle: { color: 'rgba(255,255,255,0.08)' },
              },
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
              show: true,
              position: 'right',
              min: this.currentLimits.min,
              max: this.currentLimits.max,
              axisLabel: {
                formatter: '{value} A',
                color: 'rgba(255,255,255,0.5)',
                fontSize: 10,
              },
              axisLine: { show: false },
              splitLine: {
                lineStyle: { color: 'rgba(255,255,255,0.08)' },
              },
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
              show: false,
            },
            tooltip: {
              trigger: 'axis',
              axisPointer: { type: 'shadow' },
            },
            grid: {
              left: 60,
              right: 24,
              top: 40,
              bottom: 40,
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
                formatter: '{value} mV',
                color: 'rgba(255,255,255,0.75)',
              },
              axisLine: { lineStyle: { color: 'rgba(255,255,255,0.25)' } },
              splitLine: {
                lineStyle: { color: 'rgba(255,255,255,0.1)' },
              },
              min: UNDER_VOLTAGE_CUTOFF * 0.9,  // 10% below under-voltage
              max: OVER_VOLTAGE_CUTOFF * 1.1,   // 10% above over-voltage
            },
            series: [
              {
                name: 'Tension',
                type: 'bar',
                emphasis: { focus: 'series' },
                barWidth: '60%',
                label: {
                  show: true,
                  position: 'top',
                  color: '#ffd166',
                  fontSize: 10,
                  fontWeight: 600,
                  formatter: (params) => {
                    return '';
                  },
                },
                markLine: {
                  silent: true,
                  symbol: 'none',
                  data: [
                    {
                      name: 'Under-voltage',
                      yAxis: UNDER_VOLTAGE_CUTOFF,
                      lineStyle: {
                        color: '#5da5da',
                        type: 'dashed',
                        width: 2,
                      },
                      label: {
                        show: true,
                        position: 'insideEndTop',
                        formatter: 'Under-voltage: {c} mV',
                        color: '#5da5da',
                        fontSize: 10,
                      },
                    },
                    {
                      name: 'Over-voltage',
                      yAxis: OVER_VOLTAGE_CUTOFF,
                      lineStyle: {
                        color: '#f25f5c',
                        type: 'dashed',
                        width: 2,
                      },
                      label: {
                        show: true,
                        position: 'insideEndBottom',
                        formatter: 'Over-voltage: {c} mV',
                        color: '#f25f5c',
                        fontSize: 10,
                      },
                    },
                  ],
                },
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

    this.remainingGauge = remainingGaugeElement
      ? initChart(
          remainingGaugeElement,
          {
            tooltip: {
              formatter: ({ value }) => {
                if (value == null) return 'Temps restant indisponible';
                const hours = Math.floor(value);
                const minutes = Math.round((value - hours) * 60);
                return `${hours}h ${minutes}min restant`;
              },
            },
            series: [
              {
                name: 'Temps Restant',
                type: 'gauge',
                startAngle: 180,
                endAngle: 0,
                min: 0,
                max: 48,
                splitNumber: 8,
                center: ['50%', '70%'],
                radius: '95%',
                pointer: {
                  icon: 'path://M2.9,0.7L2.9,0.7c1.4,0,2.6,1.2,2.6,2.6v115c0,1.4-1.2,2.6-2.6,2.6l0,0c-1.4,0-2.6-1.2-2.6-2.6V3.3C0.3,1.9,1.4,0.7,2.9,0.7z',
                  length: '70%',
                  width: 6,
                  itemStyle: {
                    color: 'auto',
                  },
                },
                axisLine: {
                  lineStyle: {
                    width: 16,
                    color: [
                      [0.125, '#ef4444'],  // 0-6h: rouge (critique)
                      [0.25, '#f59e0b'],   // 6-12h: orange (attention)
                      [0.5, '#10b981'],    // 12-24h: vert (bon)
                      [1, '#00d4aa'],      // 24-48h: cyan (excellent)
                    ],
                  },
                },
                axisTick: {
                  distance: -16,
                  length: 6,
                  lineStyle: { color: 'rgba(255,255,255,0.4)', width: 2 },
                },
                splitLine: {
                  distance: -16,
                  length: 10,
                  lineStyle: { color: 'rgba(255,255,255,0.5)', width: 3 },
                },
                axisLabel: {
                  color: 'rgba(255,255,255,0.8)',
                  distance: 20,
                  fontSize: 11,
                  fontWeight: 600,
                  formatter: (value) => {
                    // Afficher les labels importants
                    if (value === 0 || value === 6 || value === 12 || value === 24 || value === 48) {
                      return value + 'h';
                    }
                    return '';
                  },
                },
                detail: {
                  valueAnimation: true,
                  fontSize: 28,
                  fontWeight: 800,
                  offsetCenter: [0, '0%'],
                  color: 'auto',
                  formatter: (value) => {
                    if (value == null) return '{value|--}{unit|h}';
                    const hours = Math.floor(value);
                    const minutes = Math.round((value - hours) * 60);
                    if (hours === 0) {
                      return `{value|${minutes}}{unit|min}`;
                    }
                    return `{value|${hours}}{unit|h ${minutes}m}`;
                  },
                  rich: {
                    value: {
                      fontSize: 32,
                      fontWeight: 800,
                      color: 'auto',
                    },
                    unit: {
                      fontSize: 16,
                      fontWeight: 600,
                      color: 'rgba(255,255,255,0.6)',
                      padding: [0, 0, 0, 4],
                    },
                  },
                },
                anchor: {
                  show: true,
                  showAbove: true,
                  size: 12,
                  itemStyle: {
                    color: 'auto',
                    borderColor: 'rgba(255,255,255,0.3)',
                    borderWidth: 2,
                  },
                },
                title: {
                  show: false,
                },
                data: [
                  {
                    value: 0,
                    name: 'Temps',
                  },
                ],
              },
            ],
          },
          { renderer: 'svg' }
        )
      : null;
  }

  /**
   * Update axis limits based on BMS register values
   * @param {Object} registers - Register data containing voltage and current limits
   */
  updateAxisLimits(registers) {
    if (!registers) {
      return;
    }

    // Extract register values
    const overvoltage_mv = registers.overvoltage_cutoff_mv || DEFAULT_OVERVOLTAGE_MV;
    const undervoltage_mv = registers.undervoltage_cutoff_mv || DEFAULT_UNDERVOLTAGE_MV;
    const peak_discharge_a = registers.peak_discharge_current_a || DEFAULT_PEAK_DISCHARGE_A;
    const charge_overcurrent_a = registers.charge_overcurrent_a || DEFAULT_CHARGE_OVERCURRENT_A;

    // Calculate nominal operating voltage (middle of under/over voltage range)
    const nominal_voltage_mv = (overvoltage_mv + undervoltage_mv) / 2;
    const nominal_pack_voltage_v = (nominal_voltage_mv * NUM_CELLS) / 1000;

    // Calculate new limits with much tighter ranges for better visibility
    // Voltage: ±10% around nominal voltage for normal operating range
    const voltage_margin = nominal_pack_voltage_v * 0.1; // 10% margin
    const newVoltageLimits = {
      min: nominal_pack_voltage_v - voltage_margin,
      max: nominal_pack_voltage_v + voltage_margin,
    };

    // Current: Use ±30% of max currents for normal operating range
    const max_discharge = peak_discharge_a * 0.3; // 30% of peak discharge
    const max_charge = charge_overcurrent_a * 0.3; // 30% of charge current
    const newCurrentLimits = {
      min: -max_charge,
      max: max_discharge,
    };

    // Update limits if they changed
    const voltageLimitsChanged =
      Math.abs(this.voltageLimits.min - newVoltageLimits.min) > 0.01 ||
      Math.abs(this.voltageLimits.max - newVoltageLimits.max) > 0.01;

    const currentLimitsChanged =
      Math.abs(this.currentLimits.min - newCurrentLimits.min) > 0.1 ||
      Math.abs(this.currentLimits.max - newCurrentLimits.max) > 0.1;

    if (voltageLimitsChanged) {
      this.voltageLimits = newVoltageLimits;
      if (this.voltageSparkline) {
        this.voltageSparkline.chart.setOption({
          yAxis: {
            min: this.voltageLimits.min,
            max: this.voltageLimits.max,
          },
        });
      }
    }

    if (currentLimitsChanged) {
      this.currentLimits = newCurrentLimits;
      if (this.currentSparkline) {
        this.currentSparkline.chart.setOption({
          yAxis: {
            min: this.currentLimits.min,
            max: this.currentLimits.max,
          },
        });
      }
    }
  }

  update({ voltage, current, soc, soh, voltagesMv, balancingStates, temperature, registers, estimatedTimeLeftSeconds } = {}) {
    this.updateAxisLimits(registers);
    this.updateGauge(soc, soh);
    this.updateSparkline({ voltage, current });
    this.updateCellChart(voltagesMv);
    this.updateTemperatureGauge(temperature);
    this.updateRemainingGauge(estimatedTimeLeftSeconds);
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

  updateRemainingGauge(estimatedTimeLeftSeconds) {
    if (!this.remainingGauge) {
      return;
    }
    // Convertir les secondes en heures (décimal)
    const seconds = sanitizeNumber(estimatedTimeLeftSeconds);
    const hours = seconds != null && seconds > 0 ? seconds / 3600 : null;

    // Limiter à 48h max pour l'affichage
    const displayValue = hours != null ? Math.min(hours, 48) : null;

    this.remainingGauge.chart.setOption({
      series: [
        {
          data: [
            {
              value: displayValue,
              name: 'Temps',
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
      return Number.isFinite(number) ? number : null;
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
    this.cellVoltages = resolvedVoltages.map(v => v / 1000); // Keep in V for compatibility
    const categories = resolvedVoltages.map((_, index) => `Cellule ${index + 1}`);
    const maxVoltage = Math.max(...resolvedVoltages, 0);
    const minVoltage = Math.min(...resolvedVoltages.filter(v => v > 0), Infinity);

    // Find indices of min and max voltage cells
    const maxIndex = resolvedVoltages.indexOf(maxVoltage);
    const minIndex = resolvedVoltages.indexOf(minVoltage);

    // Calculate in-balance (difference from average in mV)
    const validVoltages = resolvedVoltages.filter(v => v > 0);
    const avgVoltage = validVoltages.length > 0
      ? validVoltages.reduce((sum, v) => sum + v, 0) / validVoltages.length
      : 0;
    const inBalanceMv = resolvedVoltages.map((value) => value > 0 ? (value - avgVoltage) : 0);

    // Create data array with color coding for min/max cells
    const chartData = resolvedVoltages.map((value, index) => {
      let color = '#00a896'; // Default green color
      if (index === maxIndex && value > 0) {
        color = '#f25f5c'; // Red for max voltage
      } else if (index === minIndex && value > 0) {
        color = '#5da5da'; // Blue for min voltage
      }
      return {
        value: value,
        itemStyle: {
          color: color,
          borderRadius: [5, 5, 0, 0],
        },
      };
    });

    const tooltipFormatter = (params = []) => {
      if (!params.length) {
        return '';
      }
      const index = params[0]?.dataIndex ?? 0;
      const voltageValue = resolvedVoltages[index];
      const diffMax = maxVoltage - voltageValue;
      const inBalance = inBalanceMv[index];
      const inBalanceSign = inBalance >= 0 ? '+' : '';

      let status = '';
      if (index === maxIndex && voltageValue > 0) {
        status = '<br/><span style="color:#f25f5c">⬆ Cellule MAX</span>';
      } else if (index === minIndex && voltageValue > 0) {
        status = '<br/><span style="color:#5da5da">⬇ Cellule MIN</span>';
      }

      return [
        `Cellule ${index + 1}`,
        `Tension: ${voltageValue.toFixed(0)} mV (${(voltageValue / 1000).toFixed(3)} V)`,
        `Écart max: ${diffMax.toFixed(1)} mV`,
        `In-balance: ${inBalanceSign}${inBalance.toFixed(1)} mV`,
      ].join('<br/>') + status;
    };

    this.cellChart.chart.setOption({
      title: { show: false },
      tooltip: { formatter: tooltipFormatter },
      xAxis: { data: categories },
      series: [
        {
          data: chartData,
          label: {
            formatter: (params) => {
              const index = params.dataIndex;
              const inBalance = inBalanceMv[index];
              if (Math.abs(inBalance) < 0.5) {
                return '';
              }
              const sign = inBalance >= 0 ? '+' : '';
              return `${sign}${inBalance.toFixed(0)}`;
            },
          },
        },
      ],
    });
  }

}
