// dashboard.js
import { initSecurityInterceptors } from '/src/js/utils/security.js';
import { BatteryRealtimeCharts } from '/src/js/charts/batteryCharts.js';
import { EnergyCharts } from '/src/js/charts/energyCharts.js';
import { UartCharts, UartTrafficChart, UartCommandDistributionChart } from '/src/js/charts/uartCharts.js';
import { CanCharts, estimateCanBusOccupancy } from '/src/js/charts/canCharts.js';
import { initChart } from '/src/js/charts/base.js';
import { SystemStatus } from '/src/js/systemStatus.js';
import { resolveTimestampFields, resolveTimestamp } from '/src/js/utils/timestamps.js';
import { normalizeSample, parseHistoryResponse } from '/src/js/utils/history.js';
import { ConfigRegistersManager } from '/src/components/configuration/config-registers.js';
import tinyBMSConfig from '/src/components/tiny/tinybms-config.js';
import { MqttTimelineChart, MqttQosChart, MqttBandwidthChart } from '/src/js/charts/mqttDashboardCharts.js';
import { initCanTooltips } from '/src/js/utils/canTooltips.js';

initSecurityInterceptors();

/**
 * Escape HTML to prevent XSS attacks
 * @param {string} text - Text to escape
 * @returns {string} Escaped HTML-safe text
 */
function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

const MQTT_STATUS_POLL_INTERVAL_MS = 5000;
const UART_STATUS_POLL_INTERVAL_MS = 5000;
const CAN_STATUS_POLL_INTERVAL_MS = 5000;
const MAX_TIMELINE_ITEMS = 60;
const MAX_STORED_FRAMES = 300;

// === État global ===
const state = {
    telemetry: null,
    history: [],
    liveHistory: [],
    liveHistoryMeta: { total: 0, returned: 0, interval_ms: null, capacity: null },
    historyLimit: 120,
    historySource: 'live',
    archives: [],
    selectedArchive: null,
    historyDirectory: '',
    historyStorageReady: false,
    historyPage: 1,
    historyPageSize: 10,
    historyChart: null,
    registers: new Map(),
    config: { last: null },
    mqtt: {
        statusInterval: null,
        lastStatus: null,
        lastConfig: null,
        messageChart: null,
    },
    mqttDashboard: {
        timelineChart: null,
        qosChart: null,
        bandwidthChart: null,
        events: [],
        topics: new Map(),
        lastUpdate: null,
        previousStats: { published: 0, received: 0, bytesIn: 0, bytesOut: 0 },
        startTime: Date.now(),
    },
    uartDashboard: {
        statusInterval: null,
        trafficChart: null,
        commandChart: null,
        lengthChart: null,
        events: [],
        commandStats: new Map(),
        errorStats: new Map(),
        previousStats: { frames: 0, bytes: 0, decoded: 0, errors: 0 },
        lastUpdate: null,
        lastStatus: null,
    },
    canDashboard: {
        statusInterval: null,
        events: [],
        errorStats: new Map(),
        previousStats: { txFrames: 0, rxFrames: 0, txBytes: 0, rxBytes: 0, initialized: false },
        lastUpdate: null,
        lastStatus: null,
    },
    configRegisters: null,
    batteryCharts: null,
    energyCharts: null,
    systemStatus: null,
    uartRealtime: {
        frames: { raw: [], decoded: [] },
        timeline: { raw: null, decoded: null },
        charts: null,
    },
    canRealtime: {
        frames: { raw: [], decoded: [] },
        timeline: { raw: null, decoded: null },
        charts: null,
        filters: { source: 'all', windowSeconds: 300 },
    },
};

// === CLASSES ===

class HistoryChart {
    constructor(container) {
        this.element = container;
        this.chart = null;
        this.samples = [];

        if (this.element) {
            const option = {
                tooltip: {
                    trigger: 'axis',
                    axisPointer: { type: 'cross' },
                    valueFormatter: (value) => (Number.isFinite(value) ? value.toFixed(2) : '--'),
                },
                legend: { data: ['Tension', 'Courant'], top: 0 },
                grid: { left: 60, right: 60, top: 48, bottom: 80 },
                dataZoom: [
                    { type: 'inside', throttle: 50 },
                    { type: 'slider', height: 26, bottom: 24, handleSize: 16 },
                ],
                xAxis: {
                    type: 'time',
                    boundaryGap: false,
                    axisLabel: { formatter: (value) => new Date(value).toLocaleTimeString() },
                },
                yAxis: [
                    { type: 'value', name: 'Tension (V)' },
                    { type: 'value', name: 'Courant (A)' },
                ],
                series: [
                    {
                        name: 'Tension',
                        type: 'line',
                        smooth: true,
                        showSymbol: false,
                        yAxisIndex: 0,
                        areaStyle: { opacity: 0.12 },
                        lineStyle: { width: 2 },
                        data: [],
                    },
                    {
                        name: 'Courant',
                        type: 'line',
                        smooth: true,
                        showSymbol: false,
                        yAxisIndex: 1,
                        lineStyle: { width: 2 },
                        data: [],
                    },
                ],
            };
            const { chart } = initChart(this.element, option, { renderer: 'canvas' });
            this.chart = chart;
        }
    }

    setData(samples) {
        this.samples = Array.isArray(samples) ? samples.slice() : [];
        this.render();
    }

    render() {
        if (!this.chart) return;

        const sortedSamples = this.samples.slice().sort((a, b) => resolveSampleTimestamp(a) - resolveSampleTimestamp(b));
        const buildSeries = (selector) =>
            sortedSamples.map((sample) => {
                const timestamp = resolveSampleTimestamp(sample) || Date.now();
                const rawValue = Number(selector(sample));
                return [timestamp, Number.isFinite(rawValue) ? rawValue : null];
            });

        const voltageData = buildSeries((s) => s.pack_voltage);
        const currentData = buildSeries((s) => s.pack_current);
        const hasData = voltageData.some(([, v]) => v != null) || currentData.some(([, v]) => v != null);

        this.chart.setOption(
            {
                series: [
                    { name: 'Tension', data: voltageData },
                    { name: 'Courant', data: currentData },
                ],
                xAxis: { min: hasData ? 'dataMin' : null, max: hasData ? 'dataMax' : null },
                graphic: hasData
                    ? []
                    : [{
                          type: 'text',
                          left: 'center',
                          top: 'middle',
                          style: { text: 'Aucune donnée disponible', fill: 'rgba(240, 248, 255, 0.75)', fontSize: 16, fontWeight: 500 },
                      }],
            },
            { lazyUpdate: true }
        );
    }
}

class MqttMessageChart {
    constructor(container) {
        this.element = container;
        this.chart = null;
        this.data = [];

        if (this.element) {
            const option = {
                tooltip: { trigger: 'item' },
                legend: { show: false },
                grid: { left: '56%', right: '4%', top: 48, bottom: 48 },
                xAxis: {
                    type: 'value',
                    axisLine: { show: false },
                    splitLine: { lineStyle: { type: 'dashed', color: 'rgba(255,255,255,0.12)' } },
                    axisLabel: { color: 'rgba(255,255,255,0.7)' },
                },
                yAxis: {
                    type: 'category',
                    inverse: true,
                    axisLine: { show: false },
                    axisTick: { show: false },
                    axisLabel: { color: 'rgba(255,255,255,0.7)' },
                    data: [],
                },
                series: [
                    {
                        name: 'Flux MQTT',
                        type: 'pie',
                        center: ['25%', '50%'],
                        radius: ['30%', '65%'],
                        label: {
                            show: true,
                            formatter: '{b}\n{c}',
                            color: 'rgba(255,255,255,0.85)'
                        },
                        itemStyle: { borderWidth: 1, borderColor: 'rgba(255,255,255,0.2)' },
                        emphasis: {
                            itemStyle: {
                                shadowBlur: 10,
                                shadowOffsetX: 0,
                                shadowColor: 'rgba(0, 0, 0, 0.5)'
                            }
                        },
                        data: [],
                    },
                    {
                        name: 'Messages',
                        type: 'bar',
                        barWidth: 14,
                        label: { show: true, position: 'right', formatter: '{c}' },
                        data: [],
                    },
                ],
            };
            const { chart } = initChart(this.element, option, { renderer: 'canvas' });
            this.chart = chart;
        }
    }

    setData(entries) {
        this.data = Array.isArray(entries)
            ? entries.filter((e) => Number.isFinite(e.value) && e.value >= 0)
            : [];
        this.render();
    }

    render() {
        if (!this.chart) return;

        const total = this.data.reduce((sum, e) => sum + e.value, 0);
        const categories = this.data.map((e) => e.label);
        const pieData = this.data.map((e) => ({ name: e.label, value: e.value }));
        const barData = this.data.map((e) => ({ name: e.label, value: e.value }));
        const hasData = pieData.length > 0;

        const tooltipFormatter = (params) => {
            const name = params.name || params.data?.name || '';
            const value = Number(params.value);
            if (!Number.isFinite(value)) return `${name}: --`;
            const ratio = total > 0 ? (value / total) * 100 : 0;
            const precision = value >= 100 ? 0 : 1;
            return `${name}: ${value} msg (${ratio.toFixed(precision)}%)`;
        };

        this.chart.setOption(
            {
                tooltip: { formatter: tooltipFormatter },
                yAxis: { data: categories },
                series: [{ data: pieData }, { data: barData }],
                graphic: hasData
                    ? []
                    : [{
                          type: 'text',
                          left: 'center',
                          top: 'middle',
                          style: { text: 'Aucune donnée disponible', fill: 'rgba(240, 248, 255, 0.65)', fontSize: 14, fontWeight: 500 },
                      }],
            },
            { lazyUpdate: true }
        );
    }
}

// === FONCTIONS UTILITAIRES ===

function formatNumber(value, suffix = '', fractionDigits = 2) {
    if (value === undefined || value === null || Number.isNaN(value)) {
        return `-- ${suffix}`.trim();
    }
    return `${Number(value).toFixed(fractionDigits)} ${suffix}`.trim();
}

function formatFileSize(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value <= 0) return '';
    const megabytes = value / (1024 * 1024);
    if (megabytes >= 1) return `${megabytes.toFixed(megabytes >= 10 ? 0 : 1)} Mo`;
    const kilobytes = value / 1024;
    if (kilobytes >= 1) return `${kilobytes.toFixed(kilobytes >= 10 ? 0 : 1)} ko`;
    return `${value.toFixed(0)} octets`;
}

function resolveSampleTimestamp(sample) {
    return resolveTimestamp(sample);
}


function formatTimestamp(timestamp) {
    return new Date(timestamp).toLocaleString();
}

// === UI HELPERS ===

function setActiveTab(tabId) {
    document.querySelectorAll('.tab-button').forEach((b) => {
        b.classList.toggle('active', b.dataset.tab === tabId);
    });
    document.querySelectorAll('.tab-panel').forEach((p) => {
        p.classList.toggle('active', p.id === `tab-${tabId}`);
    });
    if (tabId === 'mqtt') refreshMqttData(true);
}

function toggleRealtimeView(prefix, value) {
    const attr = `data-${prefix}-view`;
    document.querySelectorAll(`[${attr}]`).forEach((el) => {
        el.classList.toggle('d-none', el.getAttribute(attr) !== value);
    });
}

function setupRealtimeViewControls() {
    const setupGroup = (name, prefix) => {
        document.querySelectorAll(`input[name="${name}"]`).forEach((input) => {
            input.addEventListener('change', (e) => {
                if (e.target.checked) toggleRealtimeView(prefix, e.target.value);
            });
            if (input.checked) toggleRealtimeView(prefix, input.value);
        });
    };
    setupGroup('uart-view', 'uart');
    setupGroup('can-view', 'can');
}

function refreshCanCharts() {
    if (state.canRealtime.charts) {
        state.canRealtime.charts.update({
            rawFrames: state.canRealtime.frames.raw,
            decodedFrames: state.canRealtime.frames.decoded,
            filters: state.canRealtime.filters,
        });
    }
}

function setupCanFilters() {
    const source = document.getElementById('can-filter-source');
    if (source) {
        state.canRealtime.filters.source = source.value;
        source.addEventListener('change', () => {
            state.canRealtime.filters.source = source.value;
            refreshCanCharts();
        });
    }

    const window = document.getElementById('can-filter-window');
    if (window) {
        const init = Number.parseInt(window.value, 10);
        if (Number.isFinite(init) && init > 0) state.canRealtime.filters.windowSeconds = init;
        window.addEventListener('change', () => {
            const v = Number.parseInt(window.value, 10);
            if (Number.isFinite(v) && v > 0) state.canRealtime.filters.windowSeconds = v;
            refreshCanCharts();
        });
    }
    refreshCanCharts();
}

// === MQTT ===

function extractMqttMessageBreakdown(status) {
    if (!status || typeof status !== 'object') return [];
    const entries = [];
    const add = (label, value) => {
        const v = Number(value);
        if (Number.isFinite(v) && v >= 0 && label) {
            entries.push({ label: String(label).trim(), value: v });
        }
    };

    // topic_counts
    const topicCounts = status.topic_counts || status.topics?.counts;
    if (Array.isArray(topicCounts)) {
        topicCounts.forEach((e) => e && add(e.topic || e.name || e.label, e.count ?? e.value ?? e.messages));
    } else if (topicCounts && typeof topicCounts === 'object') {
        Object.entries(topicCounts).forEach(([k, v]) => add(k, v));
    }

    // message_counts
    const msgCounts = status.message_counts || status.message_totals || status.messages;
    if (msgCounts && typeof msgCounts === 'object') {
        Object.entries(msgCounts).forEach(([k, v]) => {
            const label = k.replace(/_/g, ' ').replace(/^./, (m) => m.toUpperCase());
            add(label, v);
        });
    }

    // fallback
    const fallbacks = [
        { key: 'published_messages', label: 'Publiés' },
        { key: 'received_messages', label: 'Reçus' },
        { key: 'retained_messages', label: 'Retenus' },
        { key: 'dropped_messages', label: 'Perdus' },
    ];
    fallbacks.forEach(({ key, label }) => status[key] != null && add(label, status[key]));

    // merge duplicates
    const map = new Map();
    entries.forEach((e) => {
        const existing = map.get(e.label);
        if (existing) existing.value += e.value;
        else map.set(e.label, { ...e });
    });

    return Array.from(map.values()).sort((a, b) => b.value - a.value);
}

function updateMqttMessageChart(status) {
    if (!state.mqtt.messageChart) return;
    state.mqtt.messageChart.setData(status ? extractMqttMessageBreakdown(status) : []);
}

function updateMqttStatus(status, error) {
    updateMqttMessageChart(status);

    const badge = document.getElementById('mqtt-connection-state');
    const helper = document.getElementById('mqtt-last-error');
    if (!badge || !helper) return;

    badge.className = 'status-badge';
    helper.classList.remove('text-danger');

    const reset = () => {
        ['client-started', 'wifi-state', 'reconnect-count', 'disconnect-count', 'error-count', 'last-event', 'last-event-time'].forEach((id) => {
            const el = document.getElementById(`mqtt-${id}`);
            if (el) el.textContent = '--';
        });
    };

    if (error) {
        reset();
        badge.textContent = 'Erreur';
        badge.classList.add('status-badge--error');
        helper.textContent = error.message || 'Statut indisponible';
        helper.classList.add('text-danger');
        return;
    }

    if (!status) {
        reset();
        badge.textContent = 'Inconnu';
        badge.classList.add('status-badge--disconnected');
        helper.textContent = 'Aucune donnée';
        return;
    }

    state.mqtt.lastStatus = status;

    // Update system status
    if (state.systemStatus) {
        state.systemStatus.handleMqttStatus(status);
    }

    if (status.connected) {
        badge.textContent = 'Connecté';
        badge.classList.add('status-badge--connected');
    } else if (status.client_started) {
        badge.textContent = 'Déconnecté';
        badge.classList.add('status-badge--disconnected');
    } else {
        badge.textContent = 'Arrêté';
        badge.classList.add('status-badge--error');
    }

    helper.textContent = status.last_error ? status.last_error : 'Aucune erreur récente';
    if (status.last_error) helper.classList.add('text-danger');

    const set = (id, value) => {
        const el = document.getElementById(`mqtt-${id}`);
        if (el) el.textContent = value;
    };

    set('client-started', status.client_started ? 'Actif' : 'Arrêté');
    set('wifi-state', status.wifi_connected ? 'Connecté' : 'Déconnecté');
    set('reconnect-count', String(status.reconnects ?? 0));
    set('disconnect-count', String(status.disconnects ?? 0));
    set('error-count', String(status.errors ?? 0));
    set('last-event', status.last_event || '--');

    const ts = Number(status.last_event_timestamp_ms);
    set('last-event-time', Number.isFinite(ts) && ts > 0 ? new Date(ts).toLocaleString() : '--');
}

function setupMqttTab() {
    const refresh = document.getElementById('mqtt-refresh');
    if (refresh) refresh.addEventListener('click', () => fetchMqttStatus().catch((err) => updateMqttStatus(null, err)));

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) stopMqttStatusPolling();
        else { fetchMqttStatus().catch(() => {}); startMqttStatusPolling(); }
    });
}

function startMqttStatusPolling() {
    if (state.mqtt.statusInterval) return;
    state.mqtt.statusInterval = setInterval(() => {
        fetchMqttStatus().catch((err) => updateMqttStatus(null, err));
    }, MQTT_STATUS_POLL_INTERVAL_MS);
}

function stopMqttStatusPolling() {
    if (state.mqtt.statusInterval) {
        clearInterval(state.mqtt.statusInterval);
        state.mqtt.statusInterval = null;
    }
}

async function fetchMqttStatus() {
    const res = await fetch('/api/mqtt/status', { cache: 'no-store' });
    if (!res.ok) throw new Error('Status failed');
    const status = await res.json();
    updateMqttStatus(status);

    // Update dashboard if it's initialized
    if (state.mqttDashboard.timelineChart) {
        updateMqttDashboardKPIs(status);
    }

    return status;
}

async function fetchUartStatus() {
    const res = await fetch('/api/uart/status', { cache: 'no-store' });
    if (!res.ok) throw new Error('UART status failed');
    const status = await res.json();
    updateUartDashboard(status);
    return status;
}

async function fetchCanStatus() {
    const res = await fetch('/api/can/status', { cache: 'no-store' });
    if (!res.ok) throw new Error('CAN status failed');
    const status = await res.json();
    updateCanDashboard(status);
    return status;
}


// === MQTT DASHBOARD ===

function setupMqttDashboard() {
    const timelineEl = document.getElementById('mqtt-dash-timeline-chart');
    const qosEl = document.getElementById('mqtt-dash-qos-chart');
    const bandwidthEl = document.getElementById('mqtt-dash-bandwidth-chart');

    if (timelineEl) state.mqttDashboard.timelineChart = new MqttTimelineChart(timelineEl);
    if (qosEl) state.mqttDashboard.qosChart = new MqttQosChart(qosEl);
    if (bandwidthEl) state.mqttDashboard.bandwidthChart = new MqttBandwidthChart(bandwidthEl);

    const refreshBtn = document.getElementById('mqtt-dash-chart-refresh');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => {
            refreshMqttDashboard(true);
        });
    }

    const clearEventsBtn = document.getElementById('mqtt-dash-events-clear');
    if (clearEventsBtn) {
        clearEventsBtn.addEventListener('click', () => {
            state.mqttDashboard.events = [];
            updateMqttDashboardEvents();
        });
    }
}

function updateMqttDashboardKPIs(status) {
    if (!status) return;

    const now = Date.now();
    const timeDelta = state.mqttDashboard.lastUpdate ? (now - state.mqttDashboard.lastUpdate) / 1000 : 1;
    state.mqttDashboard.lastUpdate = now;

    // Extract current stats
    const currentPub = status.published_messages || status.message_counts?.published || 0;
    const currentSub = status.received_messages || status.message_counts?.received || 0;
    const currentBytesIn = status.bytes_received || 0;
    const currentBytesOut = status.bytes_sent || 0;

    // Calculate rates
    const pubRate = Math.max(0, (currentPub - state.mqttDashboard.previousStats.published) / timeDelta);
    const subRate = Math.max(0, (currentSub - state.mqttDashboard.previousStats.received) / timeDelta);

    // Update previous stats
    state.mqttDashboard.previousStats = {
        published: currentPub,
        received: currentSub,
        bytesIn: currentBytesIn,
        bytesOut: currentBytesOut,
    };

    // Update KPIs
    set('mqtt-dash-pub-rate', pubRate.toFixed(1));
    set('mqtt-dash-sub-rate', subRate.toFixed(1));
    set('mqtt-dash-pub-total', currentPub.toLocaleString());
    set('mqtt-dash-sub-total', currentSub.toLocaleString());

    // Topics count
    const topicCounts = status.topic_counts || [];
    const topicsCount = Array.isArray(topicCounts) ? topicCounts.length : Object.keys(topicCounts || {}).length;
    set('mqtt-dash-topics-count', topicsCount);

    // Connection badge
    const badge = document.getElementById('mqtt-dash-connection-badge');
    if (badge) {
        badge.className = 'badge status-badge';
        if (status.connected) {
            badge.textContent = 'Oui';
            badge.classList.add('status-badge--connected');
        } else {
            badge.textContent = 'Non';
            badge.classList.add('status-badge--disconnected');
        }
    }

    // Uptime
    const uptime = status.uptime_ms || (now - state.mqttDashboard.startTime);
    set('mqtt-dash-uptime', formatDuration(uptime));
    set('mqtt-dash-reconnects', status.reconnects || 0);

    // Update charts
    if (state.mqttDashboard.timelineChart) {
        state.mqttDashboard.timelineChart.addDataPoint(now, pubRate * timeDelta, subRate * timeDelta);
    }

    // QoS distribution
    if (state.mqttDashboard.qosChart) {
        const qos0 = status.qos_counts?.qos0 || status.qos0_messages || 0;
        const qos1 = status.qos_counts?.qos1 || status.qos1_messages || 0;
        const qos2 = status.qos_counts?.qos2 || status.qos2_messages || 0;
        state.mqttDashboard.qosChart.setData(qos0, qos1, qos2);
    }

    // Bandwidth
    if (state.mqttDashboard.bandwidthChart) {
        const bytesInRate = (currentBytesIn - state.mqttDashboard.previousStats.bytesIn) / timeDelta;
        const bytesOutRate = (currentBytesOut - state.mqttDashboard.previousStats.bytesOut) / timeDelta;
        state.mqttDashboard.bandwidthChart.addDataPoint(now, bytesInRate, bytesOutRate);
    }

    // Update topics table
    updateMqttDashboardTopics(status);

    // Update detailed stats
    updateMqttDashboardStats(status);
}

function updateMqttDashboardTopics(status) {
    const tbody = document.getElementById('mqtt-dash-topics-table');
    if (!tbody) return;

    const topicCounts = status.topic_counts || [];
    const topics = Array.isArray(topicCounts) ? topicCounts : Object.entries(topicCounts || {}).map(([name, count]) => ({ topic: name, count }));

    if (topics.length === 0) {
        tbody.innerHTML = '<tr><td colspan="3" class="text-muted text-center">Aucun topic actif</td></tr>';
        return;
    }

    // Update topics map with timestamps
    topics.forEach((topic) => {
        const name = topic.topic || topic.name;
        if (name) {
            const existing = state.mqttDashboard.topics.get(name);
            const count = topic.count || topic.messages || 0;
            if (!existing || existing.count !== count) {
                state.mqttDashboard.topics.set(name, {
                    count,
                    lastUpdate: Date.now(),
                });
            }
        }
    });

    // Build table rows
    const rows = Array.from(state.mqttDashboard.topics.entries())
        .sort((a, b) => b[1].count - a[1].count)
        .slice(0, 20) // Show top 20
        .map(([name, data]) => {
            const lastUpdate = new Date(data.lastUpdate).toLocaleTimeString();
            return `
                <tr>
                    <td><code class="text-muted">${escapeHtml(name)}</code></td>
                    <td class="text-end">${data.count.toLocaleString()}</td>
                    <td class="text-end text-muted small">${lastUpdate}</td>
                </tr>
            `;
        })
        .join('');

    tbody.innerHTML = rows;
}

function updateMqttDashboardEvents() {
    const container = document.getElementById('mqtt-dash-events-timeline');
    if (!container) return;

    if (state.mqttDashboard.events.length === 0) {
        container.innerHTML = '<div class="list-group-item text-muted text-center">Aucun événement</div>';
        return;
    }

    const html = state.mqttDashboard.events
        .slice(-50) // Keep last 50 events
        .reverse()
        .map((event) => {
            const time = new Date(event.timestamp).toLocaleTimeString();
            const iconClass = event.type === 'error' ? 'text-danger' : event.type === 'warning' ? 'text-warning' : 'text-success';
            const icon = event.type === 'error' ? '⚠️' : event.type === 'warning' ? '⚡' : '✓';

            return `
                <div class="list-group-item">
                    <div class="d-flex align-items-center gap-2">
                        <span class="${iconClass}">${icon}</span>
                        <div class="flex-grow-1">
                            <div class="text-truncate">${escapeHtml(event.message)}</div>
                            <small class="text-muted">${time}</small>
                        </div>
                    </div>
                </div>
            `;
        })
        .join('');

    container.innerHTML = html;
}

function addMqttDashboardEvent(message, type = 'info') {
    state.mqttDashboard.events.push({
        message,
        type,
        timestamp: Date.now(),
    });
    updateMqttDashboardEvents();
}

function updateMqttDashboardStats(status) {
    if (!status) return;

    set('mqtt-dash-client-started', status.client_started ? 'Actif' : 'Arrêté');
    set('mqtt-dash-wifi-state', status.wifi_connected ? 'Connecté' : 'Déconnecté');

    const broker = status.host ? `${status.scheme || 'mqtt'}://${status.host}:${status.port || 1883}` : '--';
    set('mqtt-dash-broker', broker);

    set('mqtt-dash-reconnect-count', status.reconnects || 0);
    set('mqtt-dash-disconnect-count', status.disconnects || 0);
    set('mqtt-dash-error-count', status.errors || 0);

    set('mqtt-dash-dropped', status.dropped_messages || 0);
    set('mqtt-dash-retained', status.retained_messages || 0);

    const avgSize = status.average_message_size ||
                    (status.bytes_sent && status.published_messages ? (status.bytes_sent / status.published_messages) : 0);
    set('mqtt-dash-avg-size', avgSize > 0 ? Math.round(avgSize) + ' B' : '--');

    set('mqtt-dash-last-event', status.last_event || '--');

    const ts = Number(status.last_event_timestamp_ms);
    set('mqtt-dash-last-event-time', Number.isFinite(ts) && ts > 0 ? new Date(ts).toLocaleString() : '--');

    const lastError = status.last_error || 'Aucune';
    set('mqtt-dash-last-error', lastError);

    // Add event on state change
    if (status.last_event && (!state.mqtt.lastStatus || status.last_event !== state.mqtt.lastStatus.last_event)) {
        const type = status.last_error ? 'error' : status.connected ? 'success' : 'warning';
        addMqttDashboardEvent(status.last_event, type);
    }
}

function refreshMqttDashboard(force = false) {
    fetchMqttStatus()
        .then((status) => {
            updateMqttDashboardKPIs(status);
        })
        .catch((err) => {
            console.error('MQTT dashboard refresh failed', err);
            addMqttDashboardEvent('Échec de la mise à jour: ' + err.message, 'error');
        });
}

// === UART DASHBOARD ===

function setupUartDashboard() {
    const trafficEl = document.getElementById('uart-dash-traffic-chart');
    if (trafficEl) {
        state.uartDashboard.trafficChart = new UartTrafficChart(trafficEl);
    }

    const commandEl = document.getElementById('uart-dash-command-chart');
    if (commandEl) {
        state.uartDashboard.commandChart = new UartCommandDistributionChart(commandEl);
    }

    const lengthEl = document.getElementById('uart-dash-length-chart');
    if (lengthEl) {
        state.uartDashboard.lengthChart = new UartCharts({
            distributionElement: lengthEl,
            emptyTitle: 'En attente de données UART…',
        });
    }

    const refreshBtn = document.getElementById('uart-dash-refresh');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => refreshUartDashboard(true));
    }

    const clearBtn = document.getElementById('uart-dash-events-clear');
    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            state.uartDashboard.events = [];
            updateUartDashboardEvents();
        });
    }

    updateUartDashboardEvents();
    renderUartCommandTable();
    renderUartErrorTable();
}

function updateUartDashboard(status) {
    if (!status) {
        return;
    }

    state.uartDashboard.lastStatus = status;

    updateUartDashboardKPIs(status);
    updateUartDashboardCharts(status);
    updateUartDashboardTables(status);
    updateUartDashboardStats(status);

    if (Array.isArray(status.events)) {
        const incoming = status.events
            .map((event) => {
                const { timestamp, timestamp_ms } = resolveTimestampFields(event, Date.now());
                return {
                    message: event.message || event.description || event.summary || 'Événement UART',
                    type: event.type || event.level || 'info',
                    timestamp,
                    timestamp_ms,
                };
            })
            .filter((event) => event.message);

        if (incoming.length > 0) {
            const existingKeys = new Set(
                state.uartDashboard.events.map((evt) => `${(evt.timestamp_ms ?? evt.timestamp) ?? 0}|${evt.message}`)
            );
            incoming.forEach((event) => {
                const key = `${(event.timestamp_ms ?? event.timestamp) ?? 0}|${event.message}`;
                if (!existingKeys.has(key)) {
                    state.uartDashboard.events.push(event);
                    existingKeys.add(key);
                }
            });
            state.uartDashboard.events = state.uartDashboard.events.slice(-50);
            updateUartDashboardEvents();
        }
    }
}

function updateUartDashboardKPIs(status) {
    if (!status) return;

    const now = Date.now();
    const timeDeltaSeconds = state.uartDashboard.lastUpdate ? Math.max((now - state.uartDashboard.lastUpdate) / 1000, 1) : 1;
    state.uartDashboard.lastUpdate = now;

    const framesTotal = Number(status.frame_counts?.total ?? status.total_frames ?? 0);
    const bytesTotal = Number(status.byte_counts?.total ?? status.total_bytes ?? 0);
    const decodedTotal = Number(status.frame_counts?.decoded ?? status.total_decoded ?? status.decoded_frames ?? 0);
    const errorsTotal = Number(status.frame_counts?.errors ?? status.total_errors ?? status.error_count ?? 0);

    const prev = state.uartDashboard.previousStats;
    const firstUpdate = prev.frames === 0 && prev.bytes === 0 && prev.decoded === 0 && prev.errors === 0;
    const frameRate = firstUpdate ? 0 : Math.max(0, (framesTotal - prev.frames) / timeDeltaSeconds);
    const byteRate = firstUpdate ? 0 : Math.max(0, (bytesTotal - prev.bytes) / timeDeltaSeconds);
    const decodedRate = firstUpdate ? 0 : Math.max(0, (decodedTotal - prev.decoded) / timeDeltaSeconds);
    const errorRatePerMinute = firstUpdate ? 0 : Math.max(0, ((errorsTotal - prev.errors) / timeDeltaSeconds) * 60);

    state.uartDashboard.previousStats = {
        frames: framesTotal,
        bytes: bytesTotal,
        decoded: decodedTotal,
        errors: errorsTotal,
    };

    set('uart-dash-frame-rate', frameRate.toFixed(1));
    set('uart-dash-frame-total', framesTotal.toLocaleString());
    set('uart-dash-byte-rate', formatBytes(byteRate));
    set('uart-dash-byte-total', formatBytes(bytesTotal));
    set('uart-dash-decoded-rate', decodedRate.toFixed(1));
    set('uart-dash-decoded-total', decodedTotal.toLocaleString());
    set('uart-dash-error-rate', errorRatePerMinute.toFixed(2));
    set('uart-dash-error-total', errorsTotal.toLocaleString());

    const isConnected = status.connected ?? status.online ?? false;

    const badge = document.getElementById('uart-dash-connection-badge');
    if (badge) {
        badge.className = 'badge status-badge';
        if (isConnected) {
            badge.textContent = 'En ligne';
            badge.classList.add('status-badge--connected');
        } else {
            badge.textContent = 'Hors ligne';
            badge.classList.add('status-badge--disconnected');
        }
    }

    if (state.systemStatus) {
        state.systemStatus.setModuleStatus('uart', isConnected ? 'ok' : 'warning');
    }
}

function updateUartDashboardCharts(status) {
    if (!status) return;

    if (state.uartDashboard.trafficChart) {
        if (Array.isArray(status.traffic_history) && status.traffic_history.length > 0) {
            state.uartDashboard.trafficChart.setData(status.traffic_history);
        } else {
            state.uartDashboard.trafficChart.clear();
        }
    }

    const commandDataset = Array.isArray(status.command_distribution)
        ? status.command_distribution
        : Array.isArray(status.command_stats)
            ? status.command_stats
            : [];
    if (state.uartDashboard.commandChart) {
        if (commandDataset.length > 0) {
            state.uartDashboard.commandChart.setData(commandDataset);
        } else {
            state.uartDashboard.commandChart.clear();
        }
    }

    if (state.uartDashboard.lengthChart) {
        state.uartDashboard.lengthChart.update({ lengthDistribution: status.length_distribution });
    }
}

function updateUartDashboardTables(status) {
    if (!status) {
        return;
    }

    state.uartDashboard.commandStats.clear();
    const commands = Array.isArray(status.command_stats) ? status.command_stats : [];
    commands.forEach((item) => {
        if (!item) return;
        const command = String(item.command ?? item.name ?? item.id ?? '').toUpperCase();
        if (!command) return;
        const count = Number(item.count ?? item.value ?? 0) || 0;
        const lastSeen = Number(item.last_seen_ms ?? item.last_seen ?? item.timestamp ?? Date.now());
        state.uartDashboard.commandStats.set(command, { count, lastSeen });
    });
    renderUartCommandTable();

    state.uartDashboard.errorStats.clear();
    const errors = Array.isArray(status.error_breakdown) ? status.error_breakdown : [];
    errors.forEach((item) => {
        if (!item) return;
        const type = String(item.type ?? item.code ?? item.error ?? 'Erreur');
        const count = Number(item.count ?? item.value ?? 0) || 0;
        const lastSeen = Number(item.last_seen_ms ?? item.last_seen ?? item.timestamp ?? Date.now());
        state.uartDashboard.errorStats.set(type, { count, lastSeen });
    });
    renderUartErrorTable();
}

function renderUartCommandTable() {
    const tbody = document.getElementById('uart-dash-commands-table');
    if (!tbody) return;

    const rows = Array.from(state.uartDashboard.commandStats.entries())
        .sort((a, b) => b[1].count - a[1].count)
        .slice(0, 20)
        .map(([command, data]) => {
            const lastSeen = Number(data.lastSeen) ? new Date(data.lastSeen).toLocaleTimeString() : '--';
            return `
                <tr>
                    <td><code class="text-muted">${command}</code></td>
                    <td class="text-end">${data.count.toLocaleString()}</td>
                    <td class="text-end text-muted small">${lastSeen}</td>
                </tr>
            `;
        })
        .join('');

    tbody.innerHTML = rows || '<tr><td colspan="3" class="text-muted text-center">Aucune commande récente</td></tr>';
}

function renderUartErrorTable() {
    const tbody = document.getElementById('uart-dash-errors-table');
    if (!tbody) return;

    const rows = Array.from(state.uartDashboard.errorStats.entries())
        .sort((a, b) => b[1].count - a[1].count)
        .map(([type, data]) => {
            const lastSeen = Number(data.lastSeen) ? new Date(data.lastSeen).toLocaleTimeString() : '--';
            return `
                <tr>
                    <td>${type}</td>
                    <td class="text-end">${data.count.toLocaleString()}</td>
                    <td class="text-end text-muted small">${lastSeen}</td>
                </tr>
            `;
        })
        .join('');

    tbody.innerHTML = rows || '<tr><td colspan="3" class="text-muted text-center">Aucune erreur détectée</td></tr>';
}

function updateUartDashboardEvents() {
    const container = document.getElementById('uart-dash-events-timeline');
    if (!container) return;

    if (!state.uartDashboard.events.length) {
        container.innerHTML = '<div class="list-group-item text-muted text-center">Aucun événement</div>';
        return;
    }

    const html = state.uartDashboard.events
        .slice(-50)
        .reverse()
        .map((event) => {
            const time = new Date(event.timestamp).toLocaleTimeString();
            const type = event.type === 'error' ? 'danger' : event.type === 'warning' ? 'warning' : 'success';
            const icon = event.type === 'error' ? 'ti ti-alert-triangle' : event.type === 'warning' ? 'ti ti-alert-circle' : 'ti ti-check';
            return `
                <div class="list-group-item">
                    <div class="d-flex align-items-center gap-2">
                        <span class="text-${type}"><i class="${icon}"></i></span>
                        <div class="flex-grow-1">
                            <div class="text-truncate">${event.message}</div>
                            <small class="text-muted">${time}</small>
                        </div>
                    </div>
                </div>
            `;
        })
        .join('');

    container.innerHTML = html;
}

function addUartDashboardEvent(message, type = 'info', timestamp = Date.now()) {
    state.uartDashboard.events.push({ message, type, timestamp, timestamp_ms: timestamp });
    state.uartDashboard.events = state.uartDashboard.events.slice(-50);
    updateUartDashboardEvents();
}

function updateUartDashboardStats(status) {
    const uptime = Number(status.uptime_ms ?? status.uptime ?? 0);
    set('uart-dash-uptime', formatDuration(uptime));

    const lastFrameTs = Number(status.last_frame_timestamp_ms ?? status.last_frame_ts ?? 0);
    set('uart-dash-last-frame', lastFrameTs ? new Date(lastFrameTs).toLocaleTimeString() : '--');
    const lastLength = Number(status.last_frame_length ?? status.last_length ?? 0);
    set('uart-dash-last-length', lastLength > 0 ? `${lastLength} B` : '--');

    const framesTotal = Number(status.frame_counts?.total ?? status.total_frames ?? 0);
    set('uart-dash-total-frames', framesTotal.toLocaleString());
    const bytesTotal = Number(status.byte_counts?.total ?? status.total_bytes ?? 0);
    set('uart-dash-total-bytes', formatBytes(bytesTotal));
    const decodedTotal = Number(status.frame_counts?.decoded ?? status.total_decoded ?? status.decoded_frames ?? 0);
    set('uart-dash-total-decoded', decodedTotal.toLocaleString());

    const errorsTotal = Number(status.frame_counts?.errors ?? status.total_errors ?? status.error_count ?? 0);
    set('uart-dash-total-errors', errorsTotal.toLocaleString());

    const successRate = framesTotal > 0 ? ((framesTotal - errorsTotal) / framesTotal) * 100 : 0;
    set('uart-dash-success-rate', framesTotal > 0 ? `${successRate.toFixed(1)} %` : '--');

    const lastError = status.last_error || 'Aucune';
    set('uart-dash-last-error', lastError);

    const lastErrorTs = Number(status.last_error_timestamp_ms ?? status.last_error_ts ?? 0);
    set('uart-dash-last-error-time', lastErrorTs ? new Date(lastErrorTs).toLocaleString() : '--');

    const lastCommand = status.last_command ? String(status.last_command).toUpperCase() : '--';
    set('uart-dash-last-command', lastCommand);

    const lastAddress = status.last_address ?? status.last_node;
    set('uart-dash-last-address', lastAddress !== undefined && lastAddress !== null ? String(lastAddress) : '--');
}

function refreshUartDashboard(force = false) {
    fetchUartStatus()
        .catch((err) => {
            console.error('UART dashboard refresh failed', err);
            addUartDashboardEvent('Échec de la mise à jour: ' + err.message, 'error');
        });
}

function startUartStatusPolling() {
    if (state.uartDashboard.statusInterval) return;
    state.uartDashboard.statusInterval = setInterval(() => {
        fetchUartStatus().catch((err) => {
            console.warn('UART status polling error', err);
        });
    }, UART_STATUS_POLL_INTERVAL_MS);
}

function stopUartStatusPolling() {
    if (state.uartDashboard.statusInterval) {
        clearInterval(state.uartDashboard.statusInterval);
        state.uartDashboard.statusInterval = null;
    }
}

// === CAN DASHBOARD ===

const CAN_ERROR_LABELS = {
    tx_error_counter: 'Compteur erreurs TX',
    rx_error_counter: 'Compteur erreurs RX',
    tx_failed_count: 'Transmissions échouées',
    rx_missed_count: 'Réceptions manquées',
    arbitration_lost_count: 'Arbitrages perdus',
    bus_error_count: 'Erreurs bus',
    bus_off_count: 'Bus-off',
};

const CAN_ERROR_SEVERITY = {
    tx_error_counter: 'warning',
    rx_error_counter: 'warning',
    tx_failed_count: 'warning',
    rx_missed_count: 'warning',
    arbitration_lost_count: 'info',
    bus_error_count: 'warning',
    bus_off_count: 'error',
};

function setupCanDashboard() {
    const refreshBtn = document.getElementById('can-dash-refresh');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => refreshCanDashboard(true));
    }

    const clearBtn = document.getElementById('can-dash-events-clear');
    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            state.canDashboard.events = [];
            updateCanDashboardEvents();
        });
    }

    updateCanDashboardEvents();

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            stopCanStatusPolling();
        } else {
            fetchCanStatus().catch(() => {});
            startCanStatusPolling();
        }
    });
}

function updateCanDashboard(status) {
    if (!status) return;

    const previous = state.canDashboard.lastStatus;
    state.canDashboard.lastStatus = status;

    updateCanDashboardKPIs(status);
    updateCanDashboardBusStatus(status);
    updateCanDashboardTraffic(status);
    updateCanDashboardKeepalive(status);
    updateCanDashboardErrors(status);

    const timestamp = Number(status.timestamp_ms ?? status.timestamp ?? Date.now());

    if (previous && Boolean(previous.keepalive?.ok) !== Boolean(status.keepalive?.ok)) {
        const keepaliveOk = Boolean(status.keepalive?.ok);
        const message = keepaliveOk ? 'Keepalive CAN restauré' : 'Keepalive CAN perdu';
        addCanDashboardEvent(message, keepaliveOk ? 'success' : 'warning', timestamp);
    }

    if (previous && Boolean(previous.driver_started) !== Boolean(status.driver_started)) {
        const message = status.driver_started ? 'Driver CAN démarré' : 'Driver CAN arrêté';
        addCanDashboardEvent(message, status.driver_started ? 'success' : 'warning', timestamp);
    }

    const prevBusState = previous?.bus?.state ?? previous?.bus?.state_label;
    const currentBusState = status.bus?.state ?? status.bus?.state_label;
    if (previous && prevBusState !== currentBusState) {
        const label = status.bus?.state_label || (Number(status.bus?.state) === 1 ? 'En marche' : 'État mis à jour');
        const type = Number(status.bus?.state) === 1 ? 'success' : 'warning';
        addCanDashboardEvent(`Bus CAN: ${label}`, type, timestamp);
    }

    updateCanDashboardEvents();
}

function updateCanDashboardKPIs(status) {
    const now = Date.now();
    const timeDeltaSeconds = state.canDashboard.lastUpdate ? Math.max((now - state.canDashboard.lastUpdate) / 1000, 1) : 1;
    state.canDashboard.lastUpdate = now;

    const frames = status.frames ?? {};
    const txCount = Number(frames.tx_count ?? frames.tx ?? frames.sent ?? 0);
    const rxCount = Number(frames.rx_count ?? frames.rx ?? frames.received ?? 0);
    const txBytes = Number(frames.tx_bytes ?? frames.bytes_tx ?? frames.bytes_sent ?? 0);
    const rxBytes = Number(frames.rx_bytes ?? frames.bytes_rx ?? frames.bytes_received ?? 0);

    const prev = state.canDashboard.previousStats || { txFrames: 0, rxFrames: 0, txBytes: 0, rxBytes: 0, initialized: false };
    const firstUpdate = !prev.initialized;

    const txRate = firstUpdate ? 0 : Math.max(0, (txCount - prev.txFrames) / timeDeltaSeconds);
    const rxRate = firstUpdate ? 0 : Math.max(0, (rxCount - prev.rxFrames) / timeDeltaSeconds);

    state.canDashboard.previousStats = {
        txFrames: txCount,
        rxFrames: rxCount,
        txBytes,
        rxBytes,
        initialized: true,
    };

    set('can-dash-tx-rate', txRate.toFixed(1));
    set('can-dash-tx-total', txCount.toLocaleString());
    set('can-dash-tx-bytes', formatBytes(txBytes));
    set('can-dash-rx-rate', rxRate.toFixed(1));
    set('can-dash-rx-total', rxCount.toLocaleString());
    set('can-dash-rx-bytes', formatBytes(rxBytes));

    let occupancy = Number(status.bus?.occupancy_pct);
    let windowMs = Number(status.bus?.window_ms);
    if (!Number.isFinite(occupancy)) {
        const estimate = estimateCanBusOccupancy(state.canRealtime.frames.raw, {
            windowSeconds: Number(state.canRealtime.filters?.windowSeconds) || undefined,
        });
        occupancy = estimate.occupancyPct;
        if (!Number.isFinite(windowMs) || windowMs <= 0) {
            windowMs = estimate.windowMs;
        }
    }

    if (!Number.isFinite(windowMs) || windowMs <= 0) {
        windowMs = Number(status.keepalive?.interval_ms);
    }

    set('can-dash-occupancy', Number.isFinite(occupancy) ? occupancy.toFixed(1) : '0.0');
    set('can-dash-occupancy-window', Number.isFinite(windowMs) && windowMs > 0 ? formatDuration(windowMs) : '--');

    const driverStarted = Boolean(status.driver_started);
    const keepaliveOk = Boolean(status.keepalive?.ok);
    const busState = Number(status.bus?.state);

    if (state.systemStatus) {
        let moduleStatus = 'ok';
        if (!driverStarted || !keepaliveOk) {
            moduleStatus = 'warning';
        }
        if (Number.isFinite(busState) && busState >= 2) {
            moduleStatus = 'error';
        }
        state.systemStatus.setModuleStatus('can', moduleStatus);
    }
}

function updateCanDashboardBusStatus(status) {
    const timestamp = Number(status.timestamp_ms ?? status.timestamp ?? 0);
    set('can-dash-last-update', formatTimestampValue(timestamp));

    const driverStarted = Boolean(status.driver_started);
    set('can-dash-driver-state', driverStarted ? 'Actif' : 'Arrêté');

    const busStateValue = Number(status.bus?.state);
    const busStateLabel = status.bus?.state_label || (busStateValue === 1 ? 'En marche' : busStateValue === 2 ? 'Bus-off' : '--');
    set('can-dash-bus-state-text', busStateLabel || '--');

    const badge = document.getElementById('can-dash-bus-state-badge');
    if (badge) {
        badge.className = 'badge status-badge';
        if (busStateValue === 1) {
            badge.textContent = busStateLabel;
            badge.classList.add('status-badge--connected');
        } else if (busStateValue === 2) {
            badge.textContent = busStateLabel;
            badge.classList.add('status-badge--disconnected');
        } else if (busStateLabel && busStateLabel !== '--') {
            badge.textContent = busStateLabel;
            badge.classList.add('status-badge--warning');
        } else {
            badge.textContent = 'Inconnu';
            badge.classList.add('status-badge--unknown');
        }
    }

    const windowMs = Number(status.bus?.window_ms);
    if (Number.isFinite(windowMs) && windowMs > 0) {
        set('can-dash-bus-window', formatDuration(windowMs));
    } else {
        set('can-dash-bus-window', '--');
    }

    const errors = status.errors ?? {};
    set('can-dash-bus-off-count', Number(errors.bus_off_count ?? 0).toLocaleString());
    set('can-dash-bus-error-count', Number(errors.bus_error_count ?? 0).toLocaleString());
    set('can-dash-arbitration-count', Number(errors.arbitration_lost_count ?? 0).toLocaleString());
    set('can-dash-rx-missed-count', Number(errors.rx_missed_count ?? 0).toLocaleString());
}

function updateCanDashboardTraffic(status) {
    const tbody = document.getElementById('can-dash-traffic-table');
    if (!tbody) return;

    const frames = status.frames ?? {};
    const txCount = Number(frames.tx_count ?? frames.tx ?? frames.sent ?? 0);
    const rxCount = Number(frames.rx_count ?? frames.rx ?? frames.received ?? 0);
    const txBytes = Number(frames.tx_bytes ?? frames.bytes_tx ?? frames.bytes_sent ?? 0);
    const rxBytes = Number(frames.rx_bytes ?? frames.bytes_rx ?? frames.bytes_received ?? 0);

    const rows = [
        { label: 'Trames TX', value: txCount.toLocaleString() },
        { label: 'Trames RX', value: rxCount.toLocaleString() },
        { label: 'Octets TX', value: formatBytes(txBytes) },
        { label: 'Octets RX', value: formatBytes(rxBytes) },
    ];

    tbody.innerHTML = rows
        .map((row) => `<tr><td>${row.label}</td><td class="text-end">${row.value}</td></tr>`)
        .join('');
}

function updateCanDashboardKeepalive(status) {
    const keepalive = status.keepalive ?? {};
    const timestamp = Number(status.timestamp_ms ?? status.timestamp ?? Date.now());

    const lastTx = Number(keepalive.last_tx_ms ?? keepalive.last_tx);
    const lastRx = Number(keepalive.last_rx_ms ?? keepalive.last_rx);
    const intervalMs = Number(keepalive.interval_ms ?? keepalive.interval);
    const timeoutMs = Number(keepalive.timeout_ms ?? keepalive.timeout);
    const retryMs = Number(keepalive.retry_ms ?? keepalive.retry);
    const keepaliveOk = Boolean(keepalive.ok);

    const badge = document.getElementById('can-dash-keepalive-badge');
    if (badge) {
        badge.className = 'badge status-badge';
        if (keepaliveOk) {
            badge.textContent = 'OK';
            badge.classList.add('status-badge--connected');
        } else {
            badge.textContent = 'Inactif';
            badge.classList.add('status-badge--disconnected');
        }
    }

    const latencyLabel = Number.isFinite(lastRx) ? formatRelativeTime(lastRx, timestamp) : '--';
    set('can-dash-keepalive-latency', latencyLabel);

    set('can-dash-keepalive-last-tx', formatTimestampValue(lastTx));
    set('can-dash-keepalive-last-rx', formatTimestampValue(lastRx));
    set('can-dash-keepalive-interval', Number.isFinite(intervalMs) && intervalMs > 0 ? formatDuration(intervalMs) : '--');
    set('can-dash-keepalive-timeout', Number.isFinite(timeoutMs) && timeoutMs > 0 ? formatDuration(timeoutMs) : '--');
    set('can-dash-keepalive-retry', Number.isFinite(retryMs) && retryMs > 0 ? formatDuration(retryMs) : '--');

    if (Number.isFinite(lastTx)) {
        set('can-dash-last-tx', formatTimestampValue(lastTx));
    }
    if (Number.isFinite(lastRx)) {
        set('can-dash-last-rx', formatTimestampValue(lastRx));
    }
}

function updateCanDashboardErrors(status) {
    const errors = status.errors ?? {};
    const timestamp = Number(status.timestamp_ms ?? status.timestamp ?? Date.now());

    Object.entries(CAN_ERROR_LABELS).forEach(([key, label]) => {
        const count = Number(errors[key] ?? 0);
        const existing = state.canDashboard.errorStats.get(label);
        const changed = !existing || existing.count !== count;
        const lastSeen = count > 0
            ? (changed ? timestamp : existing?.lastSeen ?? timestamp)
            : existing?.lastSeen ?? null;

        if (existing && count > existing.count) {
            const diff = count - existing.count;
            const severity = CAN_ERROR_SEVERITY[key] || 'info';
            const message = diff === 1 ? `${label}: +1` : `${label}: +${diff}`;
            addCanDashboardEvent(message, severity, timestamp);
            if (severity === 'error') {
                set('can-dash-last-error', message);
                set('can-dash-last-error-time', formatTimestampValue(timestamp));
            }
        }

        state.canDashboard.errorStats.set(label, { count, lastSeen });
    });

    renderCanErrorTable();
}

function renderCanErrorTable() {
    const tbody = document.getElementById('can-dash-errors-table');
    if (!tbody) return;

    const rows = Array.from(state.canDashboard.errorStats.entries())
        .sort((a, b) => b[1].count - a[1].count)
        .map(([label, data]) => {
            const relative = data.lastSeen ? formatRelativeTime(data.lastSeen) : '--';
            return `
                <tr>
                    <td>${label}</td>
                    <td class="text-end">${Number(data.count || 0).toLocaleString()}</td>
                    <td class="text-end text-muted small">${relative}</td>
                </tr>
            `;
        })
        .join('');

    tbody.innerHTML = rows || '<tr><td colspan="3" class="text-muted text-center">Aucun compteur disponible</td></tr>';
}

function updateCanDashboardEvents() {
    const container = document.getElementById('can-dash-events');
    if (!container) return;

    if (!state.canDashboard.events.length) {
        container.innerHTML = '<div class="list-group-item text-muted text-center">Aucun événement</div>';
        return;
    }

    const html = state.canDashboard.events
        .slice(-50)
        .reverse()
        .map((event) => {
            const type = event.type === 'error' ? 'danger' : event.type === 'warning' ? 'warning' : event.type === 'success' ? 'success' : 'info';
            const icon = event.type === 'error' ? 'ti ti-alert-triangle' : event.type === 'warning' ? 'ti ti-alert-circle' : event.type === 'success' ? 'ti ti-check' : 'ti ti-info-circle';
            const date = new Date(event.timestamp);
            const time = Number.isNaN(date.getTime()) ? '--' : date.toLocaleTimeString();
            return `
                <div class="list-group-item">
                    <div class="d-flex align-items-center gap-2">
                        <span class="text-${type}"><i class="${icon}"></i></span>
                        <div class="flex-grow-1">
                            <div class="text-truncate">${event.message}</div>
                            <small class="text-muted">${time}</small>
                        </div>
                    </div>
                </div>
            `;
        })
        .join('');

    container.innerHTML = html;
}

function addCanDashboardEvent(message, type = 'info', timestamp = Date.now()) {
    state.canDashboard.events.push({ message, type, timestamp, timestamp_ms: timestamp });
    state.canDashboard.events = state.canDashboard.events.slice(-50);
    updateCanDashboardEvents();

    if (type === 'error') {
        set('can-dash-last-error', message);
        set('can-dash-last-error-time', formatTimestampValue(timestamp));
    }
}

function refreshCanDashboard() {
    fetchCanStatus().catch((err) => {
        console.error('CAN dashboard refresh failed', err);
        addCanDashboardEvent('Échec de la mise à jour CAN: ' + err.message, 'error');
    });
}

function startCanStatusPolling() {
    if (state.canDashboard.statusInterval) return;
    state.canDashboard.statusInterval = setInterval(() => {
        fetchCanStatus().catch((err) => {
            console.warn('CAN status polling error', err);
        });
    }, CAN_STATUS_POLL_INTERVAL_MS);
}

function stopCanStatusPolling() {
    if (state.canDashboard.statusInterval) {
        clearInterval(state.canDashboard.statusInterval);
        state.canDashboard.statusInterval = null;
    }
}

// === INITIALISATION ===

async function initialise() {
    const required = [
        'history-chart',
        'battery-soc-gauge',
        'uart-frames-chart',
        'history-table-body',
        'mqtt-messages-chart',
        'uart-dash-traffic-chart',
        'can-heatmap-chart',
    ];
    const missing = required.filter(id => !document.getElementById(id));
    if (missing.length > 0) {
        console.warn('Partials manquants:', missing);
        setTimeout(initialise, 100);
        return;
    }

    console.log('Dashboard initialisé');

    setupTabs();
    setupHistoryControls();
    updateArchiveControls();
    setupMqttTab();
    setupMqttDashboard();
    setupUartDashboard();
    setupCanDashboard();
    setupConfigTab();

    state.historyChart = new HistoryChart(document.getElementById('history-chart'));
    state.mqtt.messageChart = new MqttMessageChart(document.getElementById('mqtt-messages-chart'));

    state.batteryCharts = new BatteryRealtimeCharts({
        gaugeElement: document.getElementById('battery-soc-gauge'),
        voltageSparklineElement: document.getElementById('battery-voltage-sparkline'),
        currentSparklineElement: document.getElementById('battery-current-sparkline'),
        cellChartElement: document.getElementById('battery-cell-chart'),
        temperatureGaugeElement: document.getElementById('battery-temperature-gauge'),
        remainingGaugeElement: document.getElementById('battery-remaining-gauge'),
    });

    state.energyCharts = new EnergyCharts({
        energyBarChartElement: document.getElementById('energy-bar-chart'),
    });

    state.systemStatus = new SystemStatus();
    state.systemStatus.init();

    // Initialize CAN protocol tooltips
    initCanTooltips();

    state.uartRealtime.timeline.raw = document.getElementById('uart-timeline-raw');
    state.uartRealtime.timeline.decoded = document.getElementById('uart-timeline-decoded');
    state.uartRealtime.charts = new UartCharts({ distributionElement: document.getElementById('uart-frames-chart') });

    state.canRealtime.timeline.raw = document.getElementById('can-timeline-raw');
    state.canRealtime.timeline.decoded = document.getElementById('can-timeline-decoded');
    state.canRealtime.charts = new CanCharts({
        heatmapElement: document.getElementById('can-heatmap-chart'),
        throughputElement: document.getElementById('can-throughput-chart'),
    });

    setupRealtimeViewControls();
    setupCanFilters();

    try {
        await Promise.all([
            fetchStatus(),
            fetchLiveHistory(state.historyLimit),
            fetchRegisters(),
            fetchConfig().catch(() => {}),
            fetchMqttStatus().catch(() => {}),
            fetchUartStatus().catch(() => {}),
            fetchCanStatus().catch(() => {}),
        ]);
    } catch (e) { console.error('Init failed', e); }

    fetchHistoryArchives().finally(updateArchiveControls);
    startMqttStatusPolling();
    startUartStatusPolling();
    startCanStatusPolling();

    connectWebSocket('/ws/telemetry', handleTelemetryMessage);
    connectWebSocket('/ws/events', handleEventMessage);
    connectWebSocket('/ws/uart', handleUartMessage);
    connectWebSocket('/ws/can', handleCanMessage);
}

// === UTILITY FUNCTIONS ===

function set(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
}

function formatValue(value, suffix = '') {
    if (!Number.isFinite(value)) return '--';
    return value.toFixed(2) + suffix;
}

function formatDuration(ms) {
    if (!Number.isFinite(ms) || ms <= 0) return '--';
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (days > 0) return `${days}j ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
}

function formatTimestampValue(timestamp) {
    const value = Number(timestamp);
    if (!Number.isFinite(value) || value <= 0) return '--';
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return '--';
    return date.toLocaleString();
}

function formatRelativeTime(timestamp, reference = Date.now()) {
    const value = Number(timestamp);
    if (!Number.isFinite(value) || value <= 0) return '--';
    const delta = Math.max(0, reference - value);
    if (delta < 1000) return 'À l’instant';
    if (delta < 60_000) return `il y a ${Math.round(delta / 1000)} s`;
    if (delta < 3_600_000) return `il y a ${Math.round(delta / 60_000)} min`;
    if (delta < 86_400_000) return `il y a ${Math.round(delta / 3_600_000)} h`;
    return `il y a ${Math.round(delta / 86_400_000)} j`;
}

function formatBytes(value) {
    const number = Number(value);
    if (!Number.isFinite(number) || number < 0) {
        return '--';
    }

    const units = ['B', 'kB', 'MB', 'GB'];
    let unitIndex = 0;
    let scaled = number;

    while (scaled >= 1024 && unitIndex < units.length - 1) {
        scaled /= 1024;
        unitIndex += 1;
    }

    const decimals = unitIndex === 0 ? (scaled >= 10 ? 0 : 2) : scaled >= 10 ? 1 : 2;
    return `${scaled.toFixed(decimals)} ${units[unitIndex]}`;
}

// === API FUNCTIONS ===

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        if (data.battery) {
            updateBatteryDisplay(data.battery);
            // Set initial system status
            if (state.systemStatus) {
                state.systemStatus.setInitialStatus(data.battery);
            }
        }
        return data;
    } catch (error) {
        console.error('[API] fetchStatus error:', error);
        throw error;
    }
}

async function fetchLiveHistory(limit = 120) {
    try {
        const res = await fetch(`/api/history?limit=${limit}`);
        const data = await res.json();
        const { samples, total, metadata } = parseHistoryResponse(data);

        state.liveHistory = samples;
        state.liveHistoryMeta = metadata;

        if (state.historyChart) {
            state.historyChart.setData(state.liveHistory);
        }

        return { ...data, samples, total };
    } catch (error) {
        console.error('[API] fetchLiveHistory error:', error);
        throw error;
    }
}

async function fetchRegisters() {
    try {
        const res = await fetch('/api/registers');
        const data = await res.json();
        if (data.registers) {
            state.registers.clear();
            data.registers.forEach(reg => {
                state.registers.set(reg.address, reg);
            });
        }
        return data;
    } catch (error) {
        console.error('[API] fetchRegisters error:', error);
        throw error;
    }
}

async function fetchConfig() {
    try {
        const res = await fetch('/api/config');
        const data = await res.json();
        state.config.last = data;
        return data;
    } catch (error) {
        console.error('[API] fetchConfig error:', error);
        throw error;
    }
}

async function fetchHistoryArchives() {
    try {
        const res = await fetch('/api/history/files');
        const data = await res.json();
        state.archives = data.files || [];
        return data;
    } catch (error) {
        console.error('[API] fetchHistoryArchives error:', error);
        throw error;
    }
}

// === SETUP FUNCTIONS (STUBS) ===

function setupHistoryControls() {
    console.log('[Setup] History controls initialized');
    // Stub for now - history controls setup would go here
}

function updateArchiveControls() {
    console.log('[Setup] Archive controls updated');
    // Stub for now - archive controls update would go here
}

async function setupConfigTab() {
    console.log('[Setup] Config tab initialized');

    // Initialize TinyBMS configuration registers manager
    const registersContainer = document.getElementById('config-registers');
    if (registersContainer) {
        state.configRegisters = new ConfigRegistersManager();
        await state.configRegisters.init('config-registers');
        console.log('[Setup] TinyBMS registers configuration loaded');
    } else {
        console.warn('[Setup] Config registers container not found');
    }

    // Initialize TinyBMS Battery Insider configuration module
    try {
        await tinyBMSConfig.init();
        console.log('[Setup] TinyBMS Battery Insider configuration loaded');
    } catch (error) {
        console.warn('[Setup] TinyBMS Battery Insider configuration not available:', error);
    }
}

// === WEB SOCKETS ===

// Track active WebSocket connections
const activeWebSockets = new Map();
const reconnectTimeouts = new Map();
let shouldReconnectWebSockets = true;

function connectWebSocket(path, onMessage) {
    // Close existing WebSocket for this path if any
    if (activeWebSockets.has(path)) {
        const existingWs = activeWebSockets.get(path);
        try {
            existingWs.close();
        } catch (e) {
            console.warn(`[WebSocket ${path}] Error closing previous connection:`, e);
        }
        activeWebSockets.delete(path);
    }

    // Clear any pending reconnect timeout
    if (reconnectTimeouts.has(path)) {
        clearTimeout(reconnectTimeouts.get(path));
        reconnectTimeouts.delete(path);
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${protocol}//${window.location.host}${path}`;

    console.log(`[WebSocket] Connecting to ${url}...`);

    const ws = new WebSocket(url);
    activeWebSockets.set(path, ws);

    ws.onopen = () => {
        console.log(`[WebSocket] Connected to ${path}`);
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            onMessage(data);
        } catch (error) {
            console.error(`[WebSocket ${path}] Parse error:`, error);
        }
    };

    ws.onerror = (error) => {
        console.error(`[WebSocket ${path}] Error:`, error);
    };

    ws.onclose = () => {
        console.log(`[WebSocket ${path}] Disconnected`);
        activeWebSockets.delete(path);

        // Only reconnect if we should
        if (shouldReconnectWebSockets) {
            console.log(`[WebSocket ${path}] Reconnecting in 3s...`);
            const timeoutId = setTimeout(() => {
                reconnectTimeouts.delete(path);
                connectWebSocket(path, onMessage);
            }, 3000);
            reconnectTimeouts.set(path, timeoutId);
        }
    };

    return ws;
}

function disconnectAllWebSockets() {
    shouldReconnectWebSockets = false;

    // Clear all reconnect timeouts
    reconnectTimeouts.forEach((timeoutId) => {
        clearTimeout(timeoutId);
    });
    reconnectTimeouts.clear();

    // Close all active WebSockets
    activeWebSockets.forEach((ws, path) => {
        try {
            console.log(`[WebSocket] Closing ${path}...`);
            ws.close();
        } catch (e) {
            console.warn(`[WebSocket ${path}] Error closing:`, e);
        }
    });
    activeWebSockets.clear();
}

function handleTelemetryMessage(payload) {
    const data = payload && typeof payload === 'object' && payload.battery ? payload.battery : payload;
    if (!data) {
        return;
    }

    state.telemetry = data;
    updateBatteryDisplay(data);

    // Extract register values for dynamic axes
    const registerValues = {
        overvoltage_cutoff_mv: state.registers.get(315)?.value,
        undervoltage_cutoff_mv: state.registers.get(316)?.value,
        peak_discharge_current_a: state.registers.get(305)?.value,
        charge_overcurrent_a: state.registers.get(318)?.value,
    };

    // Map telemetry data to chart format
    if (state.batteryCharts) {
        state.batteryCharts.update({
            voltage: data.pack_voltage_v,
            current: data.pack_current_a,
            soc: data.state_of_charge_pct,
            soh: data.state_of_health_pct,
            voltagesMv: data.cell_voltage_mv,
            balancingStates: data.cell_balancing,
            temperature: data.average_temperature_c,
            registers: registerValues,
            estimatedTimeLeftSeconds: data.estimated_time_left_seconds,
        });
    }

    // Update energy chart
    if (state.energyCharts) {
        state.energyCharts.update({
            energyChargedWh: data.energy_charged_wh,
            energyDischargedWh: data.energy_discharged_wh,
        });
    }

    // Update system status
    if (state.systemStatus) {
        state.systemStatus.handleTelemetryUpdate(data);
    }
}

function handleEventMessage(data) {
    console.log('[Event]', data);

    // Update system status
    if (state.systemStatus) {
        state.systemStatus.handleEvent(data);
    }

    if (data.type === 'notification') {
        // Handle notifications
    }
}

function handleUartMessage(data) {
    if (!data) return;

    // Distinguish between raw and decoded frames based on the 'type' field
    const isDecoded = data.type === 'uart_decoded' || data.type === 'decoded';
    const frameArray = isDecoded ? state.uartRealtime.frames.decoded : state.uartRealtime.frames.raw;
    const timeline = isDecoded ? state.uartRealtime.timeline.decoded : state.uartRealtime.timeline.raw;

    // Add frame to appropriate array
    frameArray.push(data);
    if (frameArray.length > MAX_STORED_FRAMES) {
        frameArray.shift();
    }

    // Add item to appropriate timeline
    if (timeline) {
        addTimelineItem(timeline, data, isDecoded ? 'decoded' : 'raw');
    }

    if (!isDecoded) {
        if (state.uartRealtime.charts) {
            state.uartRealtime.charts.update({ rawFrames: state.uartRealtime.frames.raw });
        }

        if (state.uartDashboard.lengthChart) {
            state.uartDashboard.lengthChart.update({ rawFrames: state.uartRealtime.frames.raw });
        }

        if (state.uartDashboard.trafficChart) {
            const length = Number(data.length ?? data.raw?.length ?? data.bytes?.length ?? 0) || 0;
            state.uartDashboard.trafficChart.addPoint({
                timestamp_ms: data.timestamp_ms ?? Date.now(),
                frames: 1,
                bytes: length,
            });
        }

        if (Number(data.timestamp_ms)) {
            set('uart-dash-last-frame', new Date(Number(data.timestamp_ms)).toLocaleTimeString());
        }

        if (Number(data.length)) {
            set('uart-dash-last-length', `${Number(data.length)} B`);
        }
    } else {
        const command = data.command ?? data.opcode ?? data.decoded?.command;
        if (command && state.uartDashboard.commandChart) {
            state.uartDashboard.commandChart.increment(command);
        }

        if (command) {
            const normalized = String(command).toUpperCase();
            const existing = state.uartDashboard.commandStats.get(normalized) || { count: 0, lastSeen: 0 };
            state.uartDashboard.commandStats.set(normalized, {
                count: (existing.count || 0) + 1,
                lastSeen: Date.now(),
            });
            renderUartCommandTable();
            set('uart-dash-last-command', normalized);
        }

        const address = data.address ?? data.node ?? data.decoded?.address;
        if (address !== undefined && address !== null) {
            set('uart-dash-last-address', String(address));
        }

        const status = data.status ?? data.result ?? data.decoded?.status;
        const errorType = data.error_type ?? data.error ?? (status === 'error' ? 'Erreur' : null);
        if (status === 'error' || data.level === 'error' || errorType) {
            const typeLabel = errorType || 'Erreur';
            const existingError = state.uartDashboard.errorStats.get(typeLabel) || { count: 0, lastSeen: 0 };
            state.uartDashboard.errorStats.set(typeLabel, {
                count: (existingError.count || 0) + 1,
                lastSeen: Date.now(),
            });
            renderUartErrorTable();

            const timestamp = Number(data.timestamp_ms) || Date.now();
            const message = data.message || data.description || `Erreur UART (${typeLabel})`;
            addUartDashboardEvent(message, 'error', timestamp);
            set('uart-dash-last-error', message);
            set('uart-dash-last-error-time', new Date(timestamp).toLocaleString());
        }
    }
}

function handleCanMessage(data) {
    state.canRealtime.frames.raw.push(data);
    if (state.canRealtime.frames.raw.length > MAX_STORED_FRAMES) {
        state.canRealtime.frames.raw.shift();
    }

    if (state.canRealtime.timeline.raw) {
        addTimelineItem(state.canRealtime.timeline.raw, data, 'can');
    }

    if (state.canRealtime.charts) {
        state.canRealtime.charts.update({
            rawFrames: state.canRealtime.frames.raw,
            decodedFrames: state.canRealtime.frames.decoded,
            filters: state.canRealtime.filters,
        });
    }
}

function addTimelineItem(timeline, data, type) {
    const item = document.createElement('li');
    item.className = 'timeline-item';
    item.innerHTML = `
        <div class="timeline-time">${new Date(data.timestamp_ms || Date.now()).toLocaleTimeString()}</div>
        <div class="timeline-content">
            <pre>${JSON.stringify(data, null, 2)}</pre>
        </div>
    `;
    timeline.insertBefore(item, timeline.firstChild);

    // Keep only MAX_TIMELINE_ITEMS
    while (timeline.children.length > MAX_TIMELINE_ITEMS) {
        timeline.removeChild(timeline.lastChild);
    }
}

// === BATTERY DISPLAY ===

function updateKPI(data) {
    if (!data) return;

    // Update SOC/SOH
    const soc = Number.isFinite(data.state_of_charge_pct) ? data.state_of_charge_pct.toFixed(0) : '--';
    const soh = Number.isFinite(data.state_of_health_pct) ? data.state_of_health_pct.toFixed(0) : '--';
    set('kpi-soc-soh', `${soc}% / ${soh}%`);

    // Update Power (V × A)
    if (Number.isFinite(data.pack_voltage_v) && Number.isFinite(data.pack_current_a)) {
        const power = data.pack_voltage_v * data.pack_current_a;
        const powerAbs = Math.abs(power);
        const sign = power >= 0 ? '+' : '-';
        set('kpi-power', `${sign}${powerAbs.toFixed(0)} W`);
    } else {
        set('kpi-power', '-- W');
    }

    // Update Cycles
    const cycles = data.cycle_count || 0;
    set('kpi-cycles', cycles.toString());

    // Update CVL (Charge Voltage Limit) from registers or default
    const cvl_register = state.registers.get(315); // Register 315: overvoltage_cutoff_mv
    if (cvl_register && cvl_register.value) {
        const cvl_v = (cvl_register.value * 16) / 1000; // Convert mV to V for 16 cells
        set('kpi-cvl', `${cvl_v.toFixed(1)} V`);
    } else {
        set('kpi-cvl', '-- V');
    }

    // Update Time Remaining KPI
    if (data.estimated_time_left_seconds !== undefined && data.estimated_time_left_seconds > 0) {
        const hours = Math.floor(data.estimated_time_left_seconds / 3600);
        const minutes = Math.floor((data.estimated_time_left_seconds % 3600) / 60);
        if (hours > 0) {
            set('kpi-time-remaining', `${hours}h ${minutes}m`);
        } else {
            set('kpi-time-remaining', `${minutes} min`);
        }
    } else {
        set('kpi-time-remaining', '-- h');
    }
}

function updateBatteryDisplay(data) {
    if (!data) return;

    // Update KPI panel
    updateKPI(data);

    // Update voltage
    set('battery-voltage', formatValue(data.pack_voltage_v, ' V'));
    set('battery-minmax', `min ${data.min_cell_mv || 0} mV • max ${data.max_cell_mv || 0} mV`);

    // Update current
    set('battery-current', formatValue(data.pack_current_a, ' A'));
    set('battery-balancing', `Équilibrage: ${data.balancing_bits > 0 ? 'Actif' : 'Inactif'}`);

    // Update SOC/SOH
    set('battery-soc', formatValue(data.state_of_charge_pct, '%'));
    set('battery-soh', formatValue(data.state_of_health_pct, '%'));

    // Update temperatures
    set('battery-temperature', formatValue(data.average_temperature_c, ' °C'));
    set('battery-temp-extra', `MOSFET: ${formatValue(data.mosfet_temperature_c, ' °C')}`);

    // Update energy counters (CAN ID 0x378)
    const energyInKwh = data.energy_charged_wh ? (data.energy_charged_wh / 1000).toFixed(1) : '--';
    const energyOutKwh = data.energy_discharged_wh ? (data.energy_discharged_wh / 1000).toFixed(1) : '--';
    set('energy-in', `${energyInKwh} kWh`);
    set('energy-out', `${energyOutKwh} kWh`);

    // Update estimated time left
    const timeLeftEl = document.getElementById('battery-time-left');
    const timeLeftPercentBadgeEl = document.getElementById('battery-time-left-percent-badge');
    const powerBadgeEl = document.getElementById('battery-power-badge');

    if (data.estimated_time_left_seconds !== undefined && data.estimated_time_left_seconds > 0) {
        const hours = Math.floor(data.estimated_time_left_seconds / 3600);
        const minutes = Math.floor((data.estimated_time_left_seconds % 3600) / 60);
        if (timeLeftEl) timeLeftEl.textContent = `${hours} h ${minutes} min`;

        // Update SOC badge
        const soc = data.state_of_charge_pct || 0;
        if (timeLeftPercentBadgeEl) timeLeftPercentBadgeEl.textContent = `${soc.toFixed(0)}%`;

        // Update Power badge
        if (powerBadgeEl) {
            if (Number.isFinite(data.pack_voltage_v) && Number.isFinite(data.pack_current_a)) {
                const power = data.pack_voltage_v * data.pack_current_a;
                const sign = power < 0 ? '-' : '+';
                const powerAbs = Math.abs(power);
                powerBadgeEl.textContent = `${sign}${powerAbs.toFixed(0)} W`;
            } else {
                powerBadgeEl.textContent = '-- W';
            }
        }
    } else {
        if (timeLeftEl) timeLeftEl.textContent = '-- h -- min';
        if (timeLeftPercentBadgeEl) timeLeftPercentBadgeEl.textContent = '--%';
        if (powerBadgeEl) powerBadgeEl.textContent = '-- W';
    }

    // Update system info
    const sysInfo = document.getElementById('battery-system-info');
    if (sysInfo) {
        sysInfo.innerHTML = `
            <dt>Uptime</dt><dd>${formatDuration(data.uptime_seconds * 1000)}</dd>
            <dt>Cycles</dt><dd>${data.cycle_count || 0}</dd>
            <dt>Capacité</dt><dd>${data.battery_capacity_ah || 0} Ah</dd>
        `;
    }

    // Update cell voltages table
    updateCellVoltages(data.cell_voltage_mv, data.cell_balancing);

    // Update registers
    updateRegisters(data.registers || []);

    // Update TinyBMS status (Register 50)
    updateTinyBMSStatus(data.registers || []);

    // Update alarms/warnings
    updateAlarmsWarnings(data.alarm_bits, data.warning_bits);
}

function updateCellVoltages(voltages, balancing) {
    if (!voltages || !Array.isArray(voltages)) return;

    const validVoltages = voltages.filter(v => v > 0);
    const min = validVoltages.length > 0 ? Math.min(...validVoltages) : 0;
    const max = validVoltages.length > 0 ? Math.max(...validVoltages) : 0;
    const diff = max - min;
    const avg = validVoltages.length > 0 ? validVoltages.reduce((sum, v) => sum + v, 0) / validVoltages.length : 0;

    // Find indices of min and max
    const minIndex = voltages.indexOf(min);
    const maxIndex = voltages.indexOf(max);

    // Calculate max in-balance
    const inBalances = voltages.map(v => v > 0 ? Math.abs(v - avg) : 0);
    const maxInBalance = Math.max(...inBalances);

    // Count balancing cells
    const balancingCount = balancing ? balancing.filter(b => b).length : 0;

    const summary = document.getElementById('battery-cell-summary');
    if (summary) {
        summary.textContent = `Δ ${diff.toFixed(0)} mV (${min} — ${max} mV)`;
    }

    // Update cell statistics
    set('cell-stat-max', minIndex >= 0 ? `C${maxIndex + 1} (${max} mV)` : '-- (-- mV)');
    set('cell-stat-min', maxIndex >= 0 ? `C${minIndex + 1} (${min} mV)` : '-- (-- mV)');
    set('cell-stat-spread', `${diff.toFixed(0)} mV`);
    set('cell-stat-avg', `${avg.toFixed(0)} mV`);
    set('cell-stat-inbalance', `±${maxInBalance.toFixed(1)} mV`);
    set('cell-stat-balancing', `${balancingCount}/${voltages.length}`);

    // Update balancing badges
    const badges = document.getElementById('battery-balancing-badges');
    if (badges && balancing) {
        badges.innerHTML = '';
        balancing.forEach((active, index) => {
            if (active) {
                const badge = document.createElement('span');
                badge.className = 'badge bg-warning';
                badge.textContent = `C${index + 1}`;
                badges.appendChild(badge);
            }
        });
        if (badges.children.length === 0) {
            badges.innerHTML = '<span class="text-muted">Aucune cellule en équilibrage</span>';
        }
    }
}

function updateRegisters(registers) {
    const tbody = document.getElementById('battery-registers');
    if (!tbody || !Array.isArray(registers) || registers.length === 0) return;

    tbody.innerHTML = registers.map(reg => `
        <tr>
            <td>0x${reg.address?.toString(16).padStart(2, '0').toUpperCase() || '??'}</td>
            <td>0x${reg.value?.toString(16).padStart(4, '0').toUpperCase() || '????'}</td>
        </tr>
    `).join('');
}

function updateTinyBMSStatus(registers) {
    const statusBadge = document.getElementById('tinybms-status-badge');
    if (!statusBadge || !Array.isArray(registers)) return;

    // Find Register 50 (0x32 in hex) - TinyBMS Online Status
    const reg50 = registers.find(reg => reg.address === 50 || reg.address === 0x32);

    if (!reg50 || !reg50.value) {
        statusBadge.textContent = 'Inconnu';
        return;
    }

    const status = reg50.value;
    let text;

    switch (status) {
        case 0x91: // Charging
            text = 'En charge';
            break;
        case 0x92: // Fully Charged
            text = 'Chargé';
            break;
        case 0x93: // Discharging
            text = 'Décharge';
            break;
        case 0x96: // Regeneration
            text = 'Régénération';
            break;
        case 0x97: // Idle
            text = 'Au repos';
            break;
        case 0x9B: // Fault
            text = 'Défaut';
            break;
        default:
            text = `0x${status.toString(16).toUpperCase()}`;
            break;
    }

    statusBadge.textContent = text;
}

function updateAlarmsWarnings(alarms, warnings) {
    const alarmsDiv = document.getElementById('battery-alarms');
    const warningsDiv = document.getElementById('battery-warnings');

    if (alarmsDiv) {
        if (!alarms || alarms === 0) {
            alarmsDiv.innerHTML = '<div class="list-group-item text-muted">Aucune alarme</div>';
        } else {
            alarmsDiv.innerHTML = '<div class="list-group-item text-danger">Alarme active</div>';
        }
    }

    if (warningsDiv) {
        if (!warnings || warnings === 0) {
            warningsDiv.innerHTML = '<div class="list-group-item text-muted">Aucun avertissement</div>';
        } else {
            warningsDiv.innerHTML = '<div class="list-group-item text-warning">Avertissement actif</div>';
        }
    }
}

// === TAB NAVIGATION ===

function setupTabs() {
    // Use status modules as navigation buttons
    const statusModules = document.querySelectorAll('.status-module[data-tab]');
    const tabPanels = document.querySelectorAll('.tab-panel');

    statusModules.forEach(module => {
        module.addEventListener('click', () => {
            const tabId = module.getAttribute('data-tab');

            // Remove active class from all modules and panels
            statusModules.forEach(mod => mod.classList.remove('active'));
            tabPanels.forEach(panel => panel.classList.remove('active'));

            // Add active class to clicked module and corresponding panel
            module.classList.add('active');
            const panel = document.getElementById(`tab-${tabId}`);
            if (panel) {
                panel.classList.add('active');
            }
        });
    });
}

// === ATTENTE DES PARTIALS ===
function waitForPartials() {
    if (document.documentElement.dataset.partialsLoaded === 'true') {
        initialise();
        return;
    }

    const handler = () => {
        document.removeEventListener('partials-loaded', handler);
        initialise();
    };
    document.addEventListener('partials-loaded', handler);
}

waitForPartials();
window.addEventListener('beforeunload', () => {
    stopMqttStatusPolling();
    stopUartStatusPolling();
    stopCanStatusPolling();
    disconnectAllWebSockets();
});
