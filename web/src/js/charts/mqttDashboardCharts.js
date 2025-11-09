import { initChart } from './base.js';

/**
 * Timeline chart for MQTT message flow (published vs received)
 */
export class MqttTimelineChart {
  constructor(domElement) {
    if (!domElement) throw new Error('DOM element required');

    const options = {
      title: {
        show: false,
      },
      tooltip: {
        trigger: 'axis',
        axisPointer: {
          type: 'cross',
        },
      },
      legend: {
        data: ['Publiés', 'Reçus'],
        top: 0,
      },
      grid: {
        top: 40,
        left: 60,
        right: 30,
        bottom: 40,
      },
      xAxis: {
        type: 'time',
        boundaryGap: false,
        axisLabel: {
          formatter: '{HH}:{mm}:{ss}',
        },
      },
      yAxis: {
        type: 'value',
        name: 'Messages',
        minInterval: 1,
      },
      series: [
        {
          name: 'Publiés',
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: {
            width: 2,
          },
          areaStyle: {
            opacity: 0.3,
          },
          data: [],
        },
        {
          name: 'Reçus',
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: {
            width: 2,
          },
          areaStyle: {
            opacity: 0.3,
          },
          data: [],
        },
      ],
    };

    const { chart, dispose } = initChart(domElement, options);
    this.chart = chart;
    this.dispose = dispose;
    this.maxDataPoints = 60; // Keep last 60 data points
    this.publishedData = [];
    this.receivedData = [];
  }

  addDataPoint(timestamp, published, received) {
    const time = new Date(timestamp);

    this.publishedData.push([time, published || 0]);
    this.receivedData.push([time, received || 0]);

    // Keep only last N points
    if (this.publishedData.length > this.maxDataPoints) {
      this.publishedData.shift();
    }
    if (this.receivedData.length > this.maxDataPoints) {
      this.receivedData.shift();
    }

    this.chart.setOption({
      series: [
        { data: this.publishedData },
        { data: this.receivedData },
      ],
    });
  }

  clear() {
    this.publishedData = [];
    this.receivedData = [];
    this.chart.setOption({
      series: [
        { data: [] },
        { data: [] },
      ],
    });
  }
}

/**
 * QoS distribution pie chart
 */
export class MqttQosChart {
  constructor(domElement) {
    if (!domElement) throw new Error('DOM element required');

    const options = {
      title: {
        show: false,
      },
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
          name: 'QoS',
          type: 'pie',
          radius: ['40%', '70%'],
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
            itemStyle: {
              shadowBlur: 10,
              shadowOffsetX: 0,
              shadowColor: 'rgba(0, 0, 0, 0.5)',
            },
          },
          data: [],
        },
      ],
    };

    const { chart, dispose } = initChart(domElement, options);
    this.chart = chart;
    this.dispose = dispose;
  }

  setData(qos0, qos1, qos2) {
    const data = [];
    if (qos0 > 0) data.push({ value: qos0, name: 'QoS 0' });
    if (qos1 > 0) data.push({ value: qos1, name: 'QoS 1' });
    if (qos2 > 0) data.push({ value: qos2, name: 'QoS 2' });

    this.chart.setOption({
      series: [{ data }],
    });
  }
}

/**
 * Bandwidth chart showing bytes in/out over time
 */
export class MqttBandwidthChart {
  constructor(domElement) {
    if (!domElement) throw new Error('DOM element required');

    const options = {
      title: {
        show: false,
      },
      tooltip: {
        trigger: 'axis',
        axisPointer: {
          type: 'cross',
        },
        formatter: (params) => {
          let result = `${params[0].axisValueLabel}<br/>`;
          params.forEach((param) => {
            const value = param.value[1];
            const formattedValue = this.formatBytes(value);
            result += `${param.marker} ${param.seriesName}: ${formattedValue}/s<br/>`;
          });
          return result;
        },
      },
      legend: {
        data: ['Entrant', 'Sortant'],
        top: 0,
      },
      grid: {
        top: 40,
        left: 80,
        right: 30,
        bottom: 40,
      },
      xAxis: {
        type: 'time',
        boundaryGap: false,
        axisLabel: {
          formatter: '{HH}:{mm}:{ss}',
        },
      },
      yAxis: {
        type: 'value',
        name: 'Débit',
        axisLabel: {
          formatter: (value) => this.formatBytes(value),
        },
      },
      series: [
        {
          name: 'Entrant',
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: {
            width: 2,
          },
          areaStyle: {
            opacity: 0.3,
          },
          data: [],
        },
        {
          name: 'Sortant',
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: {
            width: 2,
          },
          areaStyle: {
            opacity: 0.3,
          },
          data: [],
        },
      ],
    };

    const { chart, dispose } = initChart(domElement, options);
    this.chart = chart;
    this.dispose = dispose;
    this.maxDataPoints = 60;
    this.incomingData = [];
    this.outgoingData = [];
  }

  formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
  }

  addDataPoint(timestamp, bytesIn, bytesOut) {
    const time = new Date(timestamp);

    this.incomingData.push([time, bytesIn || 0]);
    this.outgoingData.push([time, bytesOut || 0]);

    if (this.incomingData.length > this.maxDataPoints) {
      this.incomingData.shift();
    }
    if (this.outgoingData.length > this.maxDataPoints) {
      this.outgoingData.shift();
    }

    this.chart.setOption({
      series: [
        { data: this.incomingData },
        { data: this.outgoingData },
      ],
    });
  }

  clear() {
    this.incomingData = [];
    this.outgoingData = [];
    this.chart.setOption({
      series: [
        { data: [] },
        { data: [] },
      ],
    });
  }
}

/**
 * Message distribution pie chart (from existing MQTT page)
 */
export class MqttMessageChart {
  constructor(domElement) {
    if (!domElement) throw new Error('DOM element required');

    const options = {
      title: {
        show: false,
      },
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
          name: 'Messages',
          type: 'pie',
          radius: ['40%', '70%'],
          avoidLabelOverlap: true,
          itemStyle: {
            borderRadius: 8,
            borderColor: '#141b26',
            borderWidth: 2,
          },
          label: {
            show: true,
            formatter: '{b}\n{c}',
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

    const { chart, dispose } = initChart(domElement, options);
    this.chart = chart;
    this.dispose = dispose;
  }

  setData(dataArray) {
    if (!Array.isArray(dataArray)) return;

    const chartData = dataArray.map((item) => ({
      value: item.value || 0,
      name: item.label || item.name || 'Unknown',
    }));

    this.chart.setOption({
      series: [{ data: chartData }],
    });
  }
}
