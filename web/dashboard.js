// dashboard.js
import { BatteryRealtimeCharts } from '/src/js/charts/batteryCharts.js';
import { UartCharts } from '/src/js/charts/uartCharts.js';
import { CanCharts } from '/src/js/charts/canCharts.js';
import { initChart } from '/src/js/charts/base.js';

const MQTT_STATUS_POLL_INTERVAL_MS = 5000;
const MAX_TIMELINE_ITEMS = 60;
const MAX_STORED_FRAMES = 300;

// === État global ===
const state = {
    telemetry: null,
    history: [],
    liveHistory: [],
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
    batteryCharts: null,
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
    if (!sample || typeof sample !== 'object') return 0;
    const ms = Number(sample.timestamp_ms ?? sample.timestampMs);
    if (Number.isFinite(ms) && ms > 0) return ms;
    const ts = Number(sample.timestamp);
    if (Number.isFinite(ts) && ts > 0) return ts;
    if (typeof sample.timestamp_iso === 'string' && sample.timestamp_iso) {
        const parsed = Date.parse(sample.timestamp_iso);
        if (!Number.isNaN(parsed)) return parsed;
    }
    return 0;
}

function normalizeSample(raw) {
    const timestampMs = Number(raw.timestamp_ms ?? raw.timestamp ?? 0);
    let timestamp = Number(raw.timestamp ?? timestampMs);
    const iso = typeof raw.timestamp_iso === 'string' ? raw.timestamp_iso : null;
    if ((!Number.isFinite(timestamp) || timestamp <= 0) && iso) {
        const parsed = Date.parse(iso);
        if (!Number.isNaN(parsed)) timestamp = parsed;
    }

    return {
        timestamp,
        timestamp_ms: timestampMs,
        timestamp_iso: iso,
        pack_voltage: Number(raw.pack_voltage ?? raw.pack_voltage_v ?? raw.packVoltage ?? raw.pack_voltage_V ?? 0),
        pack_current: Number(raw.pack_current ?? raw.pack_current_a ?? raw.packCurrent ?? 0),
        state_of_charge: Number(raw.state_of_charge ?? raw.state_of_charge_pct ?? raw.soc ?? 0),
        state_of_health: Number(raw.state_of_health ?? raw.state_of_health_pct ?? raw.soh ?? 0),
        average_temperature: Number(raw.average_temperature ?? raw.average_temperature_c ?? raw.temperature ?? 0),
    };
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

async function fetchMqttConfig() {
    const res = await fetch('/api/mqtt/config', { cache: 'no-store' });
    if (!res.ok) throw new Error('Config failed');
    const config = await res.json();
    state.mqtt.lastConfig = config;

    const set = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.value = value ?? '';
    };
    const topics = config.topics || {};

    set('mqtt-scheme', config.scheme || 'mqtt');
    set('mqtt-host', config.host || '');
    set('mqtt-port', config.port != null ? String(config.port) : '');
    set('mqtt-username', config.username || '');
    set('mqtt-password', config.password || '');
    set('mqtt-keepalive', config.keepalive != null ? String(config.keepalive) : '');
    set('mqtt-qos', config.default_qos != null ? String(config.default_qos) : '');
    const retain = document.getElementById('mqtt-retain');
    if (retain) retain.checked = Boolean(config.retain);

    set('mqtt-status-topic', topics.status || '');
    set('mqtt-metrics-topic', topics.metrics || '');
    set('mqtt-config-topic', topics.config || '');
    set('mqtt-can-raw-topic', topics.can_raw || '');
    set('mqtt-can-decoded-topic', topics.can_decoded || '');
    set('mqtt-can-ready-topic', topics.can_ready || '');

    displayMqttMessage('');
}

function displayMqttMessage(msg, error = false) {
    const el = document.getElementById('mqtt-config-message');
    if (!el) return;
    el.textContent = msg;
    el.classList.toggle('text-danger', error);
    el.classList.toggle('text-success', !error && msg);
}

async function handleMqttSubmit(e) {
    e.preventDefault();
    const form = e.currentTarget;
    const btn = form.querySelector('button[type="submit"]');
    if (btn) btn.disabled = true;
    displayMqttMessage('Enregistrement…');

    try {
        const payload = {
            scheme: form.scheme?.value || 'mqtt',
            host: form.host?.value?.trim() || '',
            port: Number.parseInt(form.port?.value, 10) || 0,
            username: form.username?.value?.trim() || '',
            password: form.password?.value || '',
            retain: form.retain?.checked || false,
            status_topic: form.status_topic?.value?.trim() || '',
            metrics_topic: form.metrics_topic?.value?.trim() || '',
            config_topic: form.config_topic?.value?.trim() || '',
            can_raw_topic: form.can_raw_topic?.value?.trim() || '',
            can_decoded_topic: form.can_decoded_topic?.value?.trim() || '',
            can_ready_topic: form.can_ready_topic?.value?.trim() || '',
        };
        const keepalive = Number.parseInt(form.keepalive?.value, 10);
        if (!Number.isNaN(keepalive)) payload.keepalive = keepalive;
        const qos = Number.parseInt(form.default_qos?.value, 10);
        if (!Number.isNaN(qos)) payload.default_qos = qos;

        const res = await fetch('/api/mqtt/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (!res.ok) throw new Error(await res.text() || 'Update failed');

        await Promise.all([fetchMqttConfig(), fetchMqttStatus().catch(() => {})]);
        displayMqttMessage('Configuration mise à jour.', false);
    } catch (err) {
        displayMqttMessage(`Échec: ${err.message}`, true);
    } finally {
        if (btn) btn.disabled = false;
    }
}

function setupMqttTab() {
    const form = document.getElementById('mqtt-config-form');
    if (form) form.addEventListener('submit', handleMqttSubmit);

    const refresh = document.getElementById('mqtt-refresh');
    if (refresh) refresh.addEventListener('click', () => refreshMqttData(true));

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) stopMqttStatusPolling();
        else { refreshMqttData(); startMqttStatusPolling(); }
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
}

function refreshMqttData(force = false) {
    fetchMqttStatus().catch((err) => updateMqttStatus(null, err));
    if (force || !state.mqtt.lastConfig) {
        fetchMqttConfig().catch((err) => displayMqttMessage('Config MQTT échouée.', true));
    }
}

// === INITIALISATION ===

async function initialise() {
    const required = ['history-chart', 'battery-soc-gauge', 'uart-frames-chart', 'history-table-body', 'mqtt-messages-chart'];
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
    setupConfigTab();

    state.historyChart = new HistoryChart(document.getElementById('history-chart'));
    state.mqtt.messageChart = new MqttMessageChart(document.getElementById('mqtt-messages-chart'));

    state.batteryCharts = new BatteryRealtimeCharts({
        gaugeElement: document.getElementById('battery-soc-gauge'),
        voltageSparklineElement: document.getElementById('battery-voltage-sparkline'),
        currentSparklineElement: document.getElementById('battery-current-sparkline'),
        cellChartElement: document.getElementById('battery-cell-chart'),
        temperatureGaugeElement: document.getElementById('battery-temperature-gauge'),
    });

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
            fetchMqttConfig().catch(() => {}),
            fetchMqttStatus().catch(() => {}),
        ]);
    } catch (e) { console.error('Init failed', e); }

    fetchHistoryArchives().finally(updateArchiveControls);
    startMqttStatusPolling();

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

// === API FUNCTIONS ===

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        if (data.battery) {
            updateBatteryDisplay(data.battery);
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
        state.liveHistory = data.entries || [];
        if (state.historyChart) {
            state.historyChart.setData(state.liveHistory);
        }
        return data;
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

function setupConfigTab() {
    console.log('[Setup] Config tab initialized');
    // Stub for now - config tab setup would go here
}

// === WEB SOCKETS ===

function connectWebSocket(path, onMessage) {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${protocol}//${window.location.host}${path}`;

    console.log(`[WebSocket] Connecting to ${url}...`);

    const ws = new WebSocket(url);

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
        console.log(`[WebSocket ${path}] Disconnected. Reconnecting in 3s...`);
        setTimeout(() => connectWebSocket(path, onMessage), 3000);
    };

    return ws;
}

function handleTelemetryMessage(data) {
    state.telemetry = data;
    updateBatteryDisplay(data);

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
        });
    }
}

function handleEventMessage(data) {
    console.log('[Event]', data);
    if (data.type === 'notification') {
        // Handle notifications
    }
}

function handleUartMessage(data) {
    state.uartRealtime.frames.raw.push(data);
    if (state.uartRealtime.frames.raw.length > MAX_STORED_FRAMES) {
        state.uartRealtime.frames.raw.shift();
    }

    if (state.uartRealtime.timeline.raw) {
        addTimelineItem(state.uartRealtime.timeline.raw, data, 'raw');
    }

    if (state.uartRealtime.charts) {
        state.uartRealtime.charts.update({ rawFrames: state.uartRealtime.frames.raw });
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

function updateBatteryDisplay(data) {
    if (!data) return;

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
    const tabButtons = document.querySelectorAll('.tab-button');
    const tabPanels = document.querySelectorAll('.tab-panel');

    tabButtons.forEach(button => {
        button.addEventListener('click', () => {
            const tabId = button.getAttribute('data-tab');

            // Remove active class from all buttons and panels
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabPanels.forEach(panel => panel.classList.remove('active'));

            // Add active class to clicked button and corresponding panel
            button.classList.add('active');
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
window.addEventListener('beforeunload', stopMqttStatusPolling);
