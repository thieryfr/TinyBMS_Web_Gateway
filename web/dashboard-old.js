import { BatteryRealtimeCharts } from './src/js/charts/batteryCharts.js';
import { UartCharts } from './src/js/charts/uartCharts.js';
import { CanCharts } from './src/js/charts/canCharts.js';
import { initChart } from './src/js/charts/base.js';

const MQTT_STATUS_POLL_INTERVAL_MS = 5000;
const MAX_TIMELINE_ITEMS = 60;
const MAX_STORED_FRAMES = 300;

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
    config: {
        last: null,
    },
    mqtt: {
        statusInterval: null,
        lastStatus: null,
        lastConfig: null,
        messageChart: null,
    },
    batteryCharts: null,
    uartRealtime: {
        frames: {
            raw: [],
            decoded: [],
        },
        timeline: {
            raw: null,
            decoded: null,
        },
        charts: null,
    },
    canRealtime: {
        frames: {
            raw: [],
            decoded: [],
        },
        timeline: {
            raw: null,
            decoded: null,
        },
        charts: null,
        filters: {
            source: 'all',
            windowSeconds: 300,
        },
    },
};

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
                legend: {
                    data: ['Tension', 'Courant'],
                    top: 0,
                },
                grid: {
                    left: 60,
                    right: 60,
                    top: 48,
                    bottom: 80,
                },
                dataZoom: [
                    { type: 'inside', throttle: 50 },
                    { type: 'slider', height: 26, bottom: 24, handleSize: 16 },
                ],
                xAxis: {
                    type: 'time',
                    boundaryGap: false,
                    axisLabel: {
                        formatter: (value) => new Date(value).toLocaleTimeString(),
                    },
                },
                yAxis: [
                    {
                        type: 'value',
                        name: 'Tension (V)',
                        axisLabel: {
                            formatter: (value) => `${value}`,
                        },
                    },
                    {
                        type: 'value',
                        name: 'Courant (A)',
                        axisLabel: {
                            formatter: (value) => `${value}`,
                        },
                    },
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
        if (!this.chart) {
            return;
        }

        const sortedSamples = this.samples.slice().sort((a, b) => resolveSampleTimestamp(a) - resolveSampleTimestamp(b));

        const buildSeries = (selector) =>
            sortedSamples.map((sample) => {
                const timestamp = resolveSampleTimestamp(sample) || Date.now();
                const rawValue = Number(selector(sample));
                return [timestamp, Number.isFinite(rawValue) ? rawValue : null];
            });

        const voltageData = buildSeries((sample) => sample.pack_voltage);
        const currentData = buildSeries((sample) => sample.pack_current);
        const hasVoltage = voltageData.some(([, value]) => value != null);
        const hasCurrent = currentData.some(([, value]) => value != null);
        const hasData = hasVoltage || hasCurrent;

        this.chart.setOption(
            {
                series: [
                    { name: 'Tension', data: voltageData },
                    { name: 'Courant', data: currentData },
                ],
                xAxis: {
                    min: hasData ? 'dataMin' : null,
                    max: hasData ? 'dataMax' : null,
                },
                graphic: hasData
                    ? []
                    : [
                          {
                              type: 'text',
                              left: 'center',
                              top: 'middle',
                              style: {
                                  text: 'Aucune donnée disponible',
                                  fill: 'rgba(240, 248, 255, 0.75)',
                                  fontSize: 16,
                                  fontWeight: 500,
                              },
                          },
                      ],
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
                        type: 'funnel',
                        left: '3%',
                        width: '45%',
                        minSize: '20%',
                        maxSize: '100%',
                        sort: 'descending',
                        gap: 4,
                        label: { position: 'inside', formatter: '{b}\n{c}' },
                        itemStyle: { borderWidth: 1, borderColor: 'rgba(255,255,255,0.2)' },
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
            ? entries.filter((entry) => Number.isFinite(entry.value) && entry.value >= 0)
            : [];
        this.render();
    }

    render() {
        if (!this.chart) {
            return;
        }

        const total = this.data.reduce((sum, entry) => sum + entry.value, 0);
        const categories = this.data.map((entry) => entry.label);
        const funnelData = this.data.map((entry) => ({ name: entry.label, value: entry.value }));
        const barData = this.data.map((entry) => ({ name: entry.label, value: entry.value }));
        const hasData = funnelData.length > 0;

        const tooltipFormatter = (params) => {
            if (!params) {
                return '';
            }
            const name = params.name || params.data?.name || '';
            const value = Number(params.value);
            if (!Number.isFinite(value)) {
                return `${name}: --`;
            }
            const ratio = total > 0 ? (value / total) * 100 : 0;
            const precision = value >= 100 ? 0 : 1;
            return `${name}: ${value} msg (${ratio.toFixed(precision)}%)`;
        };

        this.chart.setOption(
            {
                tooltip: { formatter: tooltipFormatter },
                yAxis: { data: categories },
                series: [
                    { data: funnelData },
                    { data: barData },
                ],
                graphic: hasData
                    ? []
                    : [
                          {
                              type: 'text',
                              left: 'center',
                              top: 'middle',
                              style: {
                                  text: 'Aucune donnée disponible',
                                  fill: 'rgba(240, 248, 255, 0.65)',
                                  fontSize: 14,
                                  fontWeight: 500,
                              },
                          },
                      ],
            },
            { lazyUpdate: true }
        );
    }
}

function formatNumber(value, suffix = '', fractionDigits = 2) {
    if (value === undefined || value === null || Number.isNaN(value)) {
        return `-- ${suffix}`.trim();
    }
    return `${Number(value).toFixed(fractionDigits)} ${suffix}`.trim();
}

function formatFileSize(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value <= 0) {
        return '';
    }
    const megabytes = value / (1024 * 1024);
    if (megabytes >= 1) {
        return `${megabytes.toFixed(megabytes >= 10 ? 0 : 1)} Mo`;
    }
    const kilobytes = value / 1024;
    if (kilobytes >= 1) {
        return `${kilobytes.toFixed(kilobytes >= 10 ? 0 : 1)} ko`;
    }
    return `${value.toFixed(0)} octets`;
}

function resolveSampleTimestamp(sample) {
    if (!sample || typeof sample !== 'object') {
        return 0;
    }

    const timestampMs = Number(sample.timestamp_ms ?? sample.timestampMs);
    if (Number.isFinite(timestampMs) && timestampMs > 0) {
        return timestampMs;
    }

    const timestamp = Number(sample.timestamp);
    if (Number.isFinite(timestamp) && timestamp > 0) {
        return timestamp;
    }

    if (typeof sample.timestamp_iso === 'string' && sample.timestamp_iso) {
        const parsed = Date.parse(sample.timestamp_iso);
        if (!Number.isNaN(parsed)) {
            return parsed;
        }
    }

    return 0;
}

function normalizeSample(raw) {
    const timestampMs = Number(raw.timestamp_ms ?? raw.timestamp ?? 0);
    let timestamp = Number(raw.timestamp ?? timestampMs);
    const iso = typeof raw.timestamp_iso === 'string' ? raw.timestamp_iso : null;
    if ((!Number.isFinite(timestamp) || timestamp <= 0) && iso) {
        const parsed = Date.parse(iso);
        if (!Number.isNaN(parsed)) {
            timestamp = parsed;
        }
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
    const date = new Date(timestamp);
    return date.toLocaleString();
}

function setActiveTab(tabId) {
    document.querySelectorAll('.tab-button').forEach((button) => {
        button.classList.toggle('active', button.dataset.tab === tabId);
    });
    document.querySelectorAll('.tab-panel').forEach((panel) => {
        panel.classList.toggle('active', panel.id === `tab-${tabId}`);
    });
    if (tabId === 'mqtt') {
        refreshMqttData(true);
    }
}

function toggleRealtimeView(prefix, value) {
    const attributeName = `data-${prefix}-view`;
    document.querySelectorAll(`[${attributeName}]`).forEach((element) => {
        const elementValue = element.getAttribute(attributeName);
        element.classList.toggle('d-none', elementValue !== value);
    });
}

function setupRealtimeViewControls() {
    const setupGroup = (name, prefix) => {
        document.querySelectorAll(`input[name="${name}"]`).forEach((input) => {
            input.addEventListener('change', (event) => {
                if (event.target instanceof HTMLInputElement && event.target.checked) {
                    toggleRealtimeView(prefix, event.target.value);
                }
            });
            if (input instanceof HTMLInputElement && input.checked) {
                toggleRealtimeView(prefix, input.value);
            }
        });
    };

    setupGroup('uart-view', 'uart');
    setupGroup('can-view', 'can');
}

function setupCanFilters() {
    const sourceSelect = document.getElementById('can-filter-source');
    if (sourceSelect instanceof HTMLSelectElement) {
        state.canRealtime.filters.source = sourceSelect.value;
        sourceSelect.addEventListener('change', () => {
            state.canRealtime.filters.source = sourceSelect.value;
            refreshCanCharts();
        });
    }

    const windowSelect = document.getElementById('can-filter-window');
    if (windowSelect instanceof HTMLSelectElement) {
        const initial = Number.parseInt(windowSelect.value, 10);
        if (Number.isFinite(initial) && initial > 0) {
            state.canRealtime.filters.windowSeconds = initial;
        }
        windowSelect.addEventListener('change', () => {
            const parsed = Number.parseInt(windowSelect.value, 10);
            if (Number.isFinite(parsed) && parsed > 0) {
                state.canRealtime.filters.windowSeconds = parsed;
            }
            refreshCanCharts();
        });
    }

    refreshCanCharts();
}

function updateBatteryView(data) {
    document.getElementById('battery-voltage').textContent = formatNumber(data.pack_voltage, 'V');
    document.getElementById('battery-current').textContent = formatNumber(data.pack_current, 'A');
    document.getElementById('battery-soc').textContent = formatNumber(data.state_of_charge, '%', 1);
    document.getElementById('battery-soh').textContent = formatNumber(data.state_of_health, '%', 1);
    document.getElementById('battery-temperature').textContent = formatNumber(data.average_temperature, '°C', 1);
    document.getElementById('battery-temp-extra').textContent = `MOSFET: ${formatNumber(data.mos_temperature, '°C', 1)}`;
    document.getElementById('battery-minmax').textContent = `min ${data.min_cell_mv ?? '--'} mV • max ${data.max_cell_mv ?? '--'} mV`;

    const balancingBits = Number.isFinite(Number(data.balancing_bits))
        ? `0x${Number(data.balancing_bits).toString(16).toUpperCase()}`
        : '--';
    document.getElementById('battery-balancing').textContent = `Équilibrage: ${balancingBits}`;

    const voltagesMv = Array.isArray(data.cell_voltages_mv)
        ? data.cell_voltages_mv.map((value) => Number(value))
        : Array.isArray(data.cell_voltages)
            ? data.cell_voltages.map((value) => Number(value) * 1000)
            : null;
    const balancingStates = Array.isArray(data.cell_balancing) ? data.cell_balancing : null;

    const summaryElement = document.getElementById('battery-cell-summary');
    if (summaryElement) {
        const voltagesV = Array.isArray(voltagesMv)
            ? voltagesMv
                  .map((value) => Number(value) / 1000)
                  .filter((value) => Number.isFinite(value))
            : [];
        if (voltagesV.length > 0) {
            const minVoltage = Math.min(...voltagesV);
            const maxVoltage = Math.max(...voltagesV);
            summaryElement.textContent = `min ${minVoltage.toFixed(3)} V • max ${maxVoltage.toFixed(3)} V`;
        } else {
            summaryElement.textContent = 'Données cellules indisponibles';
        }
    }

    const badgeContainer = document.getElementById('battery-balancing-badges');
    if (badgeContainer) {
        renderBalancingBadges(badgeContainer, balancingStates);
    }

    const alarmList = document.getElementById('battery-alarms');
    if (alarmList) {
        alarmList.innerHTML = '';
        if (data.alarm_bits) {
            alarmList.appendChild(createStatusItem(`0x${data.alarm_bits.toString(16).toUpperCase()}`, 'alarm'));
        } else {
            alarmList.appendChild(createStatusItem('Aucune alarme active', 'muted'));
        }
    }

    const warningList = document.getElementById('battery-warnings');
    if (warningList) {
        warningList.innerHTML = '';
        if (data.warning_bits) {
            warningList.appendChild(createStatusItem(`0x${data.warning_bits.toString(16).toUpperCase()}`, 'warning'));
        } else {
            warningList.appendChild(createStatusItem('Aucun avertissement', 'muted'));
        }
    }

    const systemInfo = document.getElementById('battery-system-info');
    systemInfo.innerHTML = '';
    addDefinition(systemInfo, 'Horodatage', new Date(data.timestamp).toLocaleString());
    addDefinition(systemInfo, 'Uptime', `${data.uptime_seconds ?? 0} s`);
    addDefinition(systemInfo, 'Cycles', `${data.cycle_count ?? 0}`);

    const registersBody = document.getElementById('battery-registers');
    registersBody.innerHTML = '';
    if (Array.isArray(data.registers)) {
        data.registers.forEach((reg) => {
            const row = document.createElement('tr');
            const addr = document.createElement('td');
            addr.textContent = `0x${reg.address.toString(16).toUpperCase().padStart(4, '0')}`;
            const value = document.createElement('td');
            value.textContent = reg.value;
            row.append(addr, value);
            registersBody.appendChild(row);
        });
    }

    if (state.batteryCharts) {
        state.batteryCharts.update({
            voltage: data.pack_voltage,
            current: data.pack_current,
            soc: data.state_of_charge,
            soh: data.state_of_health,
            voltagesMv,
            balancingStates,
        });
    }
}

function renderBalancingBadges(container, balancingStates) {
    container.innerHTML = '';
    const totalCells = 16;

    if (!Array.isArray(balancingStates) || balancingStates.length === 0) {
        const empty = document.createElement('span');
        empty.className = 'text-secondary';
        empty.textContent = 'Équilibrage indisponible';
        container.appendChild(empty);
        return;
    }

    const fragment = document.createDocumentFragment();
    for (let index = 0; index < totalCells; index += 1) {
        const rawState = balancingStates[index];
        const isActive = Boolean(Number(rawState));
        const badge = document.createElement('span');
        badge.className = `badge ${isActive ? 'bg-green-lt text-green' : 'bg-secondary-lt text-secondary'}`;
        badge.title = `Cellule ${index + 1} ${isActive ? 'en équilibrage' : 'au repos'}`;
        badge.setAttribute('role', 'status');
        badge.dataset.state = isActive ? 'active' : 'inactive';

        const icon = document.createElement('i');
        icon.className = `ti ${isActive ? 'ti-adjustments-check' : 'ti-minus'}`;
        icon.setAttribute('aria-hidden', 'true');

        const label = document.createElement('span');
        label.textContent = `Cellule ${index + 1} · ${isActive ? 'Actif' : 'Inactif'}`;

        badge.append(icon, label);
        fragment.appendChild(badge);
    }

    container.appendChild(fragment);
}

function createStatusItem(text, variant = 'info') {
    const item = document.createElement('div');
    item.className = 'list-group-item bg-transparent';
    item.setAttribute('role', 'listitem');

    const icon = document.createElement('span');
    let iconName = 'ti-info-circle';
    let colorClass = 'text-info';

    if (variant === 'alarm') {
        iconName = 'ti-alert-triangle';
        colorClass = 'text-danger';
    } else if (variant === 'warning') {
        iconName = 'ti-alert-circle';
        colorClass = 'text-warning';
    } else if (variant === 'muted') {
        iconName = 'ti-circle-dashed';
        colorClass = 'text-secondary';
    }

    icon.className = `status-item-icon ${colorClass}`;
    icon.innerHTML = `<i class="ti ${iconName}"></i>`;
    icon.setAttribute('aria-hidden', 'true');

    const content = document.createElement('span');
    content.className = 'status-text';
    content.textContent = text;

    item.append(icon, content);
    return item;
}

function addDefinition(container, key, value) {
    const dt = document.createElement('dt');
    dt.textContent = key;
    const dd = document.createElement('dd');
    dd.textContent = value;
    container.append(dt, dd);
}

function appendToList(list, html, options = {}) {
    if (!(list instanceof Element)) {
        return;
    }

    const settings = typeof options === 'number' ? { limit: options } : options;
    const limit = Number.isFinite(settings?.limit) ? Number(settings.limit) : 100;
    const item = document.createElement('li');
    if (typeof settings?.className === 'string') {
        item.className = settings.className;
    }
    item.innerHTML = html;
    list.prepend(item);

    while (list.children.length > limit) {
        list.removeChild(list.lastElementChild);
    }
}

function parseFrameTimestamp(raw) {
    if (Number.isFinite(raw)) {
        return Number(raw);
    }
    const numeric = Number(raw);
    if (Number.isFinite(numeric)) {
        return numeric;
    }
    if (typeof raw === 'string') {
        const parsed = Date.parse(raw);
        if (!Number.isNaN(parsed)) {
            return parsed;
        }
    }
    return Date.now();
}

function formatTimeLabel(timestamp) {
    return new Date(timestamp).toLocaleTimeString([], {
        hour12: false,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
    });
}

function addTimelineEntry(list, { icon, accentClass, timeLabel, title, descriptionHtml }) {
    if (!(list instanceof Element)) {
        return;
    }

    const html = `
        <div class="timeline-item-marker ${accentClass}">
            <i class="ti ${icon}"></i>
        </div>
        <div class="timeline-item-content">
            <div class="timeline-item-time">${escapeHtml(timeLabel)}</div>
            <div class="timeline-item-title">${escapeHtml(title)}</div>
            <div class="timeline-item-description">${descriptionHtml}</div>
        </div>
    `;

    appendToList(list, html, { className: 'timeline-item', limit: MAX_TIMELINE_ITEMS });
}

function recordRealtimeFrame(collection, frame) {
    collection.push(frame);
    if (collection.length > MAX_STORED_FRAMES) {
        collection.splice(0, collection.length - MAX_STORED_FRAMES);
    }
}

function parseCanIdentifier(value) {
    if (Number.isFinite(value)) {
        return Number(value);
    }
    if (typeof value === 'string') {
        const trimmed = value.trim();
        if (trimmed.length === 0) {
            return null;
        }
        if (/^0x/i.test(trimmed)) {
            const parsed = Number.parseInt(trimmed, 16);
            return Number.isFinite(parsed) ? parsed : null;
        }
        const parsed = Number.parseInt(trimmed, 10);
        return Number.isFinite(parsed) ? parsed : null;
    }
    return null;
}

function formatCanIdentifierDisplay(value) {
    const parsed = parseCanIdentifier(value);
    if (parsed === null) {
        return 'ID inconnu';
    }
    return `0x${parsed.toString(16).toUpperCase()}`;
}

function computePayloadLength(payload) {
    if (Array.isArray(payload)) {
        return payload.length;
    }
    if (typeof payload === 'string') {
        const hex = payload.replace(/[^0-9a-f]/gi, '');
        if (hex.length === 0) {
            return 0;
        }
        return Math.ceil(hex.length / 2);
    }
    return 0;
}

function formatPayloadString(value) {
    if (typeof value === 'string') {
        return value.trim();
    }
    if (Array.isArray(value)) {
        return value
            .map((byte) => {
                const number = Number(byte);
                if (!Number.isFinite(number)) {
                    return '??';
                }
                return number.toString(16).toUpperCase().padStart(2, '0');
            })
            .join(' ');
    }
    return '';
}

function refreshUartCharts() {
    if (!state.uartRealtime.charts) {
        return;
    }
    state.uartRealtime.charts.update({
        rawFrames: state.uartRealtime.frames.raw,
        decodedFrames: state.uartRealtime.frames.decoded,
    });
}

function refreshCanCharts() {
    if (!state.canRealtime.charts) {
        return;
    }
    state.canRealtime.charts.update({
        rawFrames: state.canRealtime.frames.raw,
        decodedFrames: state.canRealtime.frames.decoded,
        filters: state.canRealtime.filters,
    });
}

async function fetchStatus() {
    const response = await fetch('/api/status');
    if (!response.ok) throw new Error('Status request failed');
    const data = await response.json();
    state.telemetry = data;
    updateBatteryView(data);
}

async function fetchLiveHistory(limit) {
    const params = new URLSearchParams();
    if (limit && Number(limit) > 0) params.set('limit', String(limit));
    const response = await fetch(`/api/history?${params.toString()}`);
    if (!response.ok) throw new Error('History request failed');
    const payload = await response.json();
    const samples = (payload.samples || []).map(normalizeSample);
    state.liveHistory = samples;
    if (state.historySource === 'live') {
        state.history = state.liveHistory;
        updateHistory(state.history);
    }
}

async function fetchArchiveSamples(file, limit) {
    if (!file) {
        state.history = [];
        updateHistory([], { preservePage: false });
        return;
    }
    const params = new URLSearchParams({ file });
    if (limit && Number(limit) > 0) params.set('limit', String(limit));
    const response = await fetch(`/api/history/archive?${params.toString()}`);
    if (!response.ok) throw new Error('Archive request failed');
    const payload = await response.json();
    const samples = (payload.samples || []).map(normalizeSample);
    state.history = samples;
    updateHistory(samples, { preservePage: false });
}

async function fetchHistory(limit) {
    if (state.historySource === 'archive') {
        await fetchArchiveSamples(state.selectedArchive, limit);
        return;
    }
    await fetchLiveHistory(limit);
}

async function fetchHistoryArchives() {
    const response = await fetch('/api/history/files');
    if (!response.ok) throw new Error('Archive list request failed');
    const payload = await response.json();
    state.historyStorageReady = Boolean(payload.flash_ready);
    state.historyDirectory = payload.directory || '';
    const files = Array.isArray(payload.files) ? payload.files : [];
    const normalized = files
        .map((file) => {
            const parsed = typeof file.modified === 'string' ? Date.parse(file.modified) : 0;
            return {
                ...file,
                modified_ms: Number.isNaN(parsed) ? 0 : parsed,
            };
        })
        .sort((a, b) => {
            if (b.modified_ms !== a.modified_ms) {
                return b.modified_ms - a.modified_ms;
            }
            return a.name.localeCompare(b.name);
        });
    state.archives = normalized;

    if (normalized.length === 0) {
        state.selectedArchive = null;
    } else if (!normalized.some((entry) => entry.name === state.selectedArchive)) {
        state.selectedArchive = normalized[0].name;
    }

    updateArchiveControls();
}

function updateArchiveControls() {
    const controls = document.getElementById('history-archive-controls');
    const select = document.getElementById('history-archive-file');
    const downloadButton = document.getElementById('history-archive-download');
    const info = document.getElementById('history-archive-info');
    const status = document.getElementById('history-storage-status');
    const rangeGroup = document.getElementById('history-range-group');
    const rangeSelect = document.getElementById('history-range');
    const directory = state.historyDirectory || '/history';

    if (status) {
        status.textContent = state.historyStorageReady ? 'Flash: disponible' : 'Flash: indisponible';
        status.classList.remove('bg-green-lt', 'text-green', 'bg-red-lt', 'text-red', 'bg-secondary-lt', 'text-secondary');
        if (state.historyStorageReady) {
            status.classList.add('bg-green-lt', 'text-green');
        } else {
            status.classList.add('bg-red-lt', 'text-red');
        }
    }

    if (controls) {
        controls.classList.toggle('d-none', state.historySource !== 'archive');
    }

    if (rangeGroup) {
        rangeGroup.classList.toggle('disabled', state.historySource === 'archive');
    }

    if (rangeSelect instanceof HTMLSelectElement) {
        rangeSelect.disabled = state.historySource === 'archive';
    }

    if (!select) {
        return;
    }

    select.innerHTML = '';
    if (state.archives.length === 0) {
        const option = document.createElement('option');
        option.textContent = state.historyStorageReady ? 'Aucun fichier disponible' : 'Stockage flash indisponible';
        option.disabled = true;
        option.selected = true;
        select.appendChild(option);
        select.disabled = true;
        if (downloadButton) downloadButton.disabled = true;
        if (info) {
            info.textContent = state.historyStorageReady
                ? `Répertoire: ${directory} • Aucun journal disponible pour le moment.`
                : `Répertoire: ${directory} • Activez la mémoire flash pour l’archivage.`;
        }
        return;
    }

    select.disabled = false;
    state.archives.forEach((file) => {
        const option = document.createElement('option');
        option.value = file.name;
        const sizeLabel = formatFileSize(file.size ?? file.size_bytes ?? 0);
        const modifiedLabel = Number.isFinite(file.modified_ms) && file.modified_ms > 0
            ? new Date(file.modified_ms).toLocaleString()
            : '';
        const parts = [file.name];
        if (modifiedLabel) {
            parts.push(modifiedLabel);
        }
        if (sizeLabel) {
            parts.push(sizeLabel);
        }
        option.textContent = parts.join(' • ');
        option.selected = file.name === state.selectedArchive;
        select.appendChild(option);
    });

    if (!state.selectedArchive && state.archives.length > 0) {
        state.selectedArchive = state.archives[0].name;
    }

    if (state.selectedArchive) {
        select.value = state.selectedArchive;
    }

    if (downloadButton) {
        downloadButton.disabled = !state.selectedArchive;
    }

    if (info) {
        const selected = state.archives.find((entry) => entry.name === state.selectedArchive);
        const sizeLabel = selected ? formatFileSize(selected.size ?? selected.size_bytes ?? 0) : '';
        const modifiedLabel = selected && Number.isFinite(selected.modified_ms) && selected.modified_ms > 0
            ? new Date(selected.modified_ms).toLocaleString()
            : '';
        const detailParts = [];
        if (selected) {
            detailParts.push(`Sélection: ${selected.name}`);
        }
        if (modifiedLabel) {
            detailParts.push(`Modifié le ${modifiedLabel}`);
        }
        if (sizeLabel) {
            detailParts.push(`Taille ${sizeLabel}`);
        }
        const detailText = detailParts.length > 0 ? ` • ${detailParts.join(' • ')}` : '';
        info.textContent = `Répertoire: ${directory} • ${state.archives.length} fichier(s)${detailText}`;
    }
}

function updateHistory(samples, options = {}) {
    const { preservePage = false } = options;
    if (!state.historyChart) {
        state.historyChart = new HistoryChart(document.getElementById('history-chart'));
    }
    state.historyChart.setData(samples);
    renderHistoryTable(samples, { preservePage });
}

function renderHistoryTable(samples, { preservePage = false } = {}) {
    const tbody = document.getElementById('history-table-body');
    if (!tbody) {
        return;
    }

    const ordered = (Array.isArray(samples) ? samples.slice() : [])
        .sort((a, b) => resolveSampleTimestamp(a) - resolveSampleTimestamp(b))
        .reverse();
    const total = ordered.length;
    const pageSize = Math.max(Number(state.historyPageSize) || 10, 1);
    const pageCount = Math.max(1, Math.ceil(total / pageSize));

    if (!preservePage) {
        state.historyPage = 1;
    }
    state.historyPage = Math.min(Math.max(state.historyPage || 1, 1), pageCount);

    const start = (state.historyPage - 1) * pageSize;
    const pageItems = ordered.slice(start, start + pageSize);

    tbody.innerHTML = pageItems
        .map((sample) => {
            const timestampText = sample.timestamp_iso
                ? new Date(sample.timestamp_iso).toLocaleString()
                : formatTimestamp(sample.timestamp);
            return `
                <tr>
                  <td>${timestampText}</td>
                  <td>${formatNumber(sample.pack_voltage, 'V')}</td>
                  <td>${formatNumber(sample.pack_current, 'A')}</td>
                  <td>${formatNumber(sample.state_of_charge, '%', 1)}</td>
                  <td>${formatNumber(sample.average_temperature, '°C', 1)}</td>
                </tr>
            `;
        })
        .join('');

    updateHistorySummary(start, pageItems.length, total);
    renderHistoryPagination(pageCount);
}

function updateHistorySummary(startIndex, count, total) {
    const summary = document.getElementById('history-table-summary');
    if (!summary) {
        return;
    }
    if (total === 0) {
        summary.textContent = 'Aucun échantillon disponible';
        return;
    }
    const from = startIndex + 1;
    const to = startIndex + count;
    summary.textContent = `Échantillons ${from}–${to} sur ${total}`;
}

function renderHistoryPagination(pageCount) {
    const container = document.getElementById('history-pagination');
    if (!container) {
        return;
    }

    if (pageCount <= 1) {
        container.innerHTML = '';
        container.setAttribute('aria-hidden', 'true');
        return;
    }

    container.removeAttribute('aria-hidden');
    const items = [];
    const current = state.historyPage;
    const createItem = (label, value, disabled = false, active = false, ariaLabel = null) => {
        const classes = ['page-item'];
        if (disabled) classes.push('disabled');
        if (active) classes.push('active');
        const attributes = [`class="${classes.join(' ')}"`];
        const buttonAttrs = [`class="page-link"`, 'type="button"', `data-page="${value}"`];
        if (disabled) {
            buttonAttrs.push('disabled');
        }
        if (active) {
            buttonAttrs.push('aria-current="page"');
        }
        if (ariaLabel) {
            buttonAttrs.push(`aria-label="${ariaLabel}"`);
        }
        return `<li ${attributes.join(' ')}><button ${buttonAttrs.join(' ')}>${label}</button></li>`;
    };

    items.push(createItem('&laquo;', 'prev', current <= 1, false, 'Page précédente'));

    const maxPages = 5;
    let start = Math.max(1, current - Math.floor(maxPages / 2));
    let end = Math.min(pageCount, start + maxPages - 1);
    if (end - start + 1 < maxPages) {
        start = Math.max(1, end - maxPages + 1);
    }

    for (let page = start; page <= end; page += 1) {
        items.push(createItem(String(page), String(page), false, page === current, `Aller à la page ${page}`));
    }

    items.push(createItem('&raquo;', 'next', current >= pageCount, false, 'Page suivante'));

    container.innerHTML = `<ul class="pagination m-0">${items.join('')}</ul>`;
}

function setInputValue(id, value) {
    const element = document.getElementById(id);
    if (!(element instanceof HTMLInputElement || element instanceof HTMLSelectElement)) {
        return;
    }

    if (value === null || value === undefined) {
        element.value = '';
    } else if (typeof value === 'number' && Number.isFinite(value)) {
        element.value = String(value);
    } else {
        element.value = String(value);
    }
}

function populateGeneralConfigForm(config) {
    if (!config) {
        return;
    }

    const device = config.device || {};
    const uart = config.uart || {};
    const wifi = config.wifi || {};
    const wifiSta = wifi.sta || {};
    const wifiAp = wifi.ap || {};
    const can = config.can || {};
    const twai = can.twai || {};
    const keepalive = can.keepalive || {};
    const publisher = can.publisher || {};
    const identity = can.identity || {};

    const pollInterval = uart.poll_interval_ms ?? config.uart_poll_interval_ms ?? '';

    setInputValue('device-name', device.name || '');
    setInputValue('uart-poll-interval', pollInterval);
    setInputValue('uart-tx-gpio', uart.tx_gpio ?? '');
    setInputValue('uart-rx-gpio', uart.rx_gpio ?? '');

    setInputValue('wifi-sta-ssid', wifiSta.ssid || '');
    setInputValue('wifi-sta-password', wifiSta.password || '');
    setInputValue('wifi-sta-hostname', wifiSta.hostname || '');
    setInputValue('wifi-sta-max-retry', wifiSta.max_retry ?? '');

    setInputValue('wifi-ap-ssid', wifiAp.ssid || '');
    setInputValue('wifi-ap-password', wifiAp.password || '');
    setInputValue('wifi-ap-channel', wifiAp.channel ?? '');
    setInputValue('wifi-ap-max-clients', wifiAp.max_clients ?? '');

    setInputValue('can-tx-gpio', twai.tx_gpio ?? '');
    setInputValue('can-rx-gpio', twai.rx_gpio ?? '');
    setInputValue('can-keepalive-interval', keepalive.interval_ms ?? '');
    setInputValue('can-keepalive-timeout', keepalive.timeout_ms ?? '');
    setInputValue('can-keepalive-retry', keepalive.retry_ms ?? '');
    setInputValue('can-publisher-period', publisher.period_ms ?? '');

    setInputValue('can-handshake', identity.handshake_ascii || '');
    setInputValue('can-manufacturer', identity.manufacturer || '');
    setInputValue('can-battery-name', identity.battery_name || '');
    setInputValue('can-battery-family', identity.battery_family || '');
    setInputValue('can-serial-number', identity.serial_number || '');
}

function buildGeneralConfigPayload(form) {
    const current = state.config.last || {};
    const wifiCurrent = current.wifi || {};
    const wifiStaCurrent = wifiCurrent.sta || {};
    const wifiApCurrent = wifiCurrent.ap || {};
    const canCurrent = current.can || {};
    const twaiCurrent = canCurrent.twai || {};
    const keepaliveCurrent = canCurrent.keepalive || {};
    const publisherCurrent = canCurrent.publisher || {};
    const identityCurrent = canCurrent.identity || {};
    const uartCurrent = current.uart || {};

    const getControl = (name) => {
        const control = form.elements.namedItem(name);
        if (control instanceof HTMLInputElement || control instanceof HTMLSelectElement) {
            return control;
        }
        return null;
    };

    const readText = (name) => {
        const control = getControl(name);
        if (!control) {
            return '';
        }
        if (control instanceof HTMLInputElement && control.type === 'password') {
            return control.value;
        }
        return control.value.trim();
    };

    const readRaw = (name) => {
        const control = getControl(name);
        return control ? control.value : '';
    };

    const readInt = (name, fallback = 0) => {
        const control = getControl(name);
        if (!control) {
            return fallback;
        }
        const value = control.value.trim();
        if (value === '') {
            return fallback;
        }
        const parsed = Number.parseInt(value, 10);
        return Number.isNaN(parsed) ? fallback : parsed;
    };

    return {
        device: {
            name: readText('device_name'),
        },
        uart: {
            poll_interval_ms: readInt(
                'uart_poll_interval_ms',
                Number.parseInt(String(uartCurrent.poll_interval_ms ?? current.uart_poll_interval_ms ?? 0), 10) || 0
            ),
            tx_gpio: readInt('uart_tx_gpio', Number.parseInt(String(uartCurrent.tx_gpio ?? 0), 10) || 0),
            rx_gpio: readInt('uart_rx_gpio', Number.parseInt(String(uartCurrent.rx_gpio ?? 0), 10) || 0),
        },
        wifi: {
            sta: {
                ssid: readText('wifi_sta_ssid'),
                password: readRaw('wifi_sta_password'),
                hostname: readText('wifi_sta_hostname'),
                max_retry: readInt('wifi_sta_max_retry', Number.parseInt(String(wifiStaCurrent.max_retry ?? 0), 10) || 0),
            },
            ap: {
                ssid: readText('wifi_ap_ssid'),
                password: readRaw('wifi_ap_password'),
                channel: readInt('wifi_ap_channel', Number.parseInt(String(wifiApCurrent.channel ?? 1), 10) || 1),
                max_clients: readInt('wifi_ap_max_clients', Number.parseInt(String(wifiApCurrent.max_clients ?? 4), 10) || 4),
            },
        },
        can: {
            twai: {
                tx_gpio: readInt('can_tx_gpio', Number.parseInt(String(twaiCurrent.tx_gpio ?? 0), 10) || 0),
                rx_gpio: readInt('can_rx_gpio', Number.parseInt(String(twaiCurrent.rx_gpio ?? 0), 10) || 0),
            },
            keepalive: {
                interval_ms: readInt(
                    'can_keepalive_interval_ms',
                    Number.parseInt(String(keepaliveCurrent.interval_ms ?? 0), 10) || 0
                ),
                timeout_ms: readInt(
                    'can_keepalive_timeout_ms',
                    Number.parseInt(String(keepaliveCurrent.timeout_ms ?? 0), 10) || 0
                ),
                retry_ms: readInt(
                    'can_keepalive_retry_ms',
                    Number.parseInt(String(keepaliveCurrent.retry_ms ?? 0), 10) || 0
                ),
            },
            publisher: {
                period_ms: readInt(
                    'can_publisher_period_ms',
                    Number.parseInt(String(publisherCurrent.period_ms ?? 0), 10) || 0
                ),
            },
            identity: {
                handshake_ascii: readText('can_handshake_ascii'),
                manufacturer: readText('can_manufacturer'),
                battery_name: readText('can_battery_name'),
                battery_family: readText('can_battery_family'),
                serial_number: readText('can_serial_number'),
            },
        },
    };
}

async function fetchConfig(options = {}) {
    const { silent = false } = options;
    if (!silent) {
        updateConfigStatus('Chargement de la configuration…', false, true);
    }

    try {
        const response = await fetch('/api/config', { cache: 'no-store' });
        if (!response.ok) {
            throw new Error('Configuration request failed');
        }
        const config = await response.json();
        state.config.last = config;
        populateGeneralConfigForm(config);
        if (!silent) {
            updateConfigStatus('Configuration chargée.', false, true);
        }
    } catch (error) {
        console.error('Failed to load configuration', error);
        updateConfigStatus('Impossible de charger la configuration.', true, true);
        throw error;
    }
}

async function fetchRegisters() {
    const response = await fetch('/api/registers');
    if (!response.ok) throw new Error('Register request failed');
    const payload = await response.json();
    const container = document.getElementById('config-registers');
    container.innerHTML = '';
    state.registers.clear();

    (payload.registers || []).forEach((reg) => {
        state.registers.set(reg.key, reg);
        container.appendChild(createRegisterCard(reg));
    });
    updateConfigStatus(`${payload.total} registres chargés.`);
}

function createRegisterCard(register) {
    const column = document.createElement('div');
    column.className = 'col-md-6 col-xl-4';

    const card = document.createElement('div');
    card.className = 'card config-card h-100';
    card.dataset.key = register.key;

    const body = document.createElement('div');
    body.className = 'card-body d-flex flex-column gap-3';

    const title = document.createElement('h3');
    title.className = 'card-title fs-5 mb-1';
    title.textContent = `${register.label}${register.unit ? ` (${register.unit})` : ''}`;

    const valueLabel = document.createElement('div');
    valueLabel.className = 'text-secondary small';
    valueLabel.textContent = `Adresse 0x${register.address.toString(16).toUpperCase()} • Min ${register.min} • Max ${register.max}`;

    const inputs = document.createElement('div');
    inputs.className = 'd-grid gap-2';

    const range = document.createElement('input');
    range.type = 'range';
    range.min = register.min;
    range.max = register.max;
    range.step = register.step;
    range.value = register.value;

    const number = document.createElement('input');
    number.type = 'number';
    number.min = register.min;
    number.max = register.max;
    number.step = register.step;
    number.value = register.value;

    range.addEventListener('input', () => {
        number.value = range.value;
    });
    number.addEventListener('input', () => {
        range.value = number.value;
    });

    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn btn-primary';
    button.textContent = 'Appliquer';
    button.addEventListener('click', async () => {
        button.disabled = true;
        try {
            const value = Number(number.value);
            await sendRegisterUpdate(register.key, value);
            updateConfigStatus(`Registre ${register.label} mis à jour.`);
        } catch (error) {
            console.error(error);
            updateConfigStatus(`Échec de la mise à jour de ${register.label}`, true);
        } finally {
            button.disabled = false;
        }
    });

    inputs.append(range, number, button);
    body.append(title, valueLabel, inputs);
    card.append(body);
    column.append(card);
    return column;
}

function updateRegisterCard(key, value) {
    const register = state.registers.get(key);
    if (!register) {
        return;
    }
    register.value = value;
    const card = document.querySelector(`.config-card[data-key="${key}"]`);
    if (!card) {
        return;
    }
    const range = card.querySelector('input[type="range"]');
    const number = card.querySelector('input[type="number"]');
    if (range) range.value = value;
    if (number) number.value = value;
}

async function sendRegisterUpdate(key, value) {
    const response = await fetch('/api/registers', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key, value }),
    });
    if (!response.ok) {
        throw new Error('Register update failed');
    }
}

async function handleGeneralConfigSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    if (!(form instanceof HTMLFormElement)) {
        return;
    }

    const submitButton = form.querySelector('button[type="submit"]');
    if (submitButton) {
        submitButton.disabled = true;
    }

    updateConfigStatus('Enregistrement de la configuration…', false, true);

    try {
        const payload = buildGeneralConfigPayload(form);
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (!response.ok) {
            const errorText = (await response.text()) || 'Configuration update failed';
            throw new Error(errorText);
        }

        await fetchConfig({ silent: true });
        updateConfigStatus('Configuration appliquée.', false, true);
    } catch (error) {
        console.error('Configuration update failed', error);
        const message = error instanceof Error ? error.message : 'Erreur inconnue';
        updateConfigStatus(`Échec de l’enregistrement: ${message}`, true, true);
    } finally {
        if (submitButton) {
            submitButton.disabled = false;
        }
    }
}

function handleGeneralConfigReset() {
    const form = document.getElementById('general-config-form');
    if (form instanceof HTMLFormElement) {
        form.classList.remove('was-validated');
    }
    if (state.config.last) {
        populateGeneralConfigForm(state.config.last);
        updateConfigStatus('Formulaire réinitialisé.');
    }
}

function updateConfigStatus(message, isError = false, force = false) {
    const status = document.getElementById('config-status');
    if (!status) {
        return;
    }

    const currentState = status.dataset.state;
    if (!force && currentState === 'error' && !isError) {
        return;
    }

    status.textContent = message;
    status.dataset.state = isError ? 'error' : 'success';
    status.className = 'badge px-3 py-2 fw-semibold';
    if (isError) {
        status.classList.add('bg-red-lt', 'text-red');
    } else {
        status.classList.add('bg-green-lt', 'text-green');
    }
}

function setupConfigTab() {
    const form = document.getElementById('general-config-form');
    if (form instanceof HTMLFormElement) {
        form.addEventListener('submit', (event) => {
            event.preventDefault();
            event.stopPropagation();
            if (!form.checkValidity()) {
                form.classList.add('was-validated');
                updateConfigStatus('Veuillez corriger les erreurs du formulaire.', true, true);
                return;
            }
            form.classList.remove('was-validated');
            handleGeneralConfigSubmit(event);
        });
    }

    const refreshButton = document.getElementById('config-refresh');
    if (refreshButton instanceof HTMLButtonElement) {
        refreshButton.addEventListener('click', () => {
            fetchConfig().catch(() => {
                // Errors already logged and surfaced via updateConfigStatus
            });
        });
    }

    const resetButton = document.getElementById('general-config-reset');
    if (resetButton instanceof HTMLButtonElement) {
        resetButton.addEventListener('click', handleGeneralConfigReset);
    }

    setupConfigAccordion();
}

function setupConfigAccordion() {
    const accordion = document.getElementById('config-accordion');
    if (!accordion) {
        return;
    }

    accordion.querySelectorAll('[data-accordion-target]').forEach((button) => {
        button.addEventListener('click', () => {
            const targetSelector = button.getAttribute('data-accordion-target');
            if (!targetSelector) {
                return;
            }
            const collapse = accordion.querySelector(targetSelector);
            if (!(collapse instanceof HTMLElement)) {
                return;
            }
            const expanded = button.getAttribute('aria-expanded') === 'true';
            button.classList.toggle('collapsed', expanded);
            button.setAttribute('aria-expanded', String(!expanded));
            collapse.classList.toggle('show', !expanded);
        });
    });
}

function pushHistorySample(sample) {
    const normalized = normalizeSample(sample);
    state.liveHistory.push(normalized);
    const limit = Number(state.historyLimit || 0);
    if (limit > 0 && state.liveHistory.length > limit) {
        state.liveHistory.splice(0, state.liveHistory.length - limit);
    }
    if (state.historySource === 'live') {
        state.history = state.liveHistory;
        const preservePage = state.historyPage > 1;
        updateHistory(state.history, { preservePage });
    }
}

function handleTelemetryMessage(event) {
    try {
        const data = JSON.parse(event.data);
        if (data.type !== 'battery') {
            return;
        }
        state.telemetry = data;
        updateBatteryView(data);
        pushHistorySample(data);
    } catch (error) {
        console.warn('Invalid telemetry payload', error);
    }
}

function handleEventMessage(event) {
    try {
        const data = JSON.parse(event.data);
        if (data.type === 'register_update') {
            updateRegisterCard(data.key, data.value);
        }
    } catch (error) {
        // Ignore non JSON notifications
    }
}

function handleUartMessage(event) {
    try {
        const data = JSON.parse(event.data);
        const timestamp = parseFrameTimestamp(data.timestamp ?? data.timestamp_ms);
        if (data.type === 'uart_raw') {
            const payload = formatPayloadString(data.data ?? data.payload ?? data.bytes);
            const length = Number.isFinite(Number(data.length))
                ? Number(data.length)
                : computePayloadLength(payload);
            const frame = {
                timestamp,
                length,
                payload,
            };
            recordRealtimeFrame(state.uartRealtime.frames.raw, frame);
            addTimelineEntry(state.uartRealtime.timeline.raw, {
                icon: 'ti-wave-square',
                accentClass: 'bg-primary',
                timeLabel: formatTimeLabel(frame.timestamp),
                title: `Trame brute (${length} octet${length > 1 ? 's' : ''})`,
                descriptionHtml:
                    frame.payload.length > 0
                        ? `<code>${escapeHtml(frame.payload)}</code>`
                        : '<span class="text-secondary">Aucune donnée</span>',
            });
            refreshUartCharts();
        } else if (data.type === 'uart_decoded') {
            const { type: _type, timestamp: _timestamp, timestamp_ms: _timestampMs, ...rest } = data;
            const summary = rest.message ?? rest.label ?? rest.description ?? 'Décodage TinyBMS';
            const frame = {
                timestamp,
                summary,
                details: rest,
            };
            recordRealtimeFrame(state.uartRealtime.frames.decoded, frame);
            const cleaned = { ...rest };
            delete cleaned.raw;
            delete cleaned.message;
            delete cleaned.label;
            delete cleaned.description;
            const description = `<pre>${escapeHtml(JSON.stringify(cleaned, null, 2))}</pre>`;
            addTimelineEntry(state.uartRealtime.timeline.decoded, {
                icon: 'ti-settings-cog',
                accentClass: 'bg-purple',
                timeLabel: formatTimeLabel(frame.timestamp),
                title: summary,
                descriptionHtml: description,
            });
            refreshUartCharts();
        }
    } catch (error) {
        console.warn('Invalid UART payload', error);
    }
}

function handleCanMessage(event) {
    try {
        const data = JSON.parse(event.data);
        const timestamp = parseFrameTimestamp(data.timestamp ?? data.timestamp_ms);
        if (data.type === 'can_raw') {
            const identifier = data.id ?? data.can_id ?? data.identifier;
            const payload = formatPayloadString(data.data ?? data.payload ?? data.bytes);
            const lengthCandidate = data.length ?? data.dlc;
            const length = Number.isFinite(Number(lengthCandidate))
                ? Number(lengthCandidate)
                : computePayloadLength(payload);
            const frame = {
                timestamp,
                id: parseCanIdentifier(identifier),
                identifier,
                length,
                payload,
                bus: data.bus ?? data.channel ?? data.port ?? null,
            };
            recordRealtimeFrame(state.canRealtime.frames.raw, frame);
            const parts = [formatCanIdentifierDisplay(identifier)];
            if (Number.isFinite(length)) {
                parts.push(`${length} octet${length > 1 ? 's' : ''}`);
            }
            if (frame.bus) {
                parts.push(`Bus ${String(frame.bus)}`);
            }
            const description =
                frame.payload.length > 0
                    ? `<code>${escapeHtml(frame.payload)}</code>`
                    : '<span class="text-secondary">Aucune donnée</span>';
            addTimelineEntry(state.canRealtime.timeline.raw, {
                icon: 'ti-antenna-bars-4',
                accentClass: 'bg-indigo',
                timeLabel: formatTimeLabel(frame.timestamp),
                title: parts.join(' • '),
                descriptionHtml: description,
            });
            refreshCanCharts();
        } else if (data.type === 'can_decoded') {
            const { type: _type, timestamp: _timestamp, timestamp_ms: _timestampMs, ...rest } = data;
            const identifier = rest.id ?? rest.can_id ?? rest.identifier ?? rest.frame_id;
            const summary = rest.description ?? rest.label ?? rest.name ?? 'Décodage CAN';
            const frame = {
                timestamp,
                id: parseCanIdentifier(identifier),
                identifier,
                summary,
                details: rest,
            };
            recordRealtimeFrame(state.canRealtime.frames.decoded, frame);
            const cleaned = { ...rest };
            delete cleaned.description;
            delete cleaned.label;
            delete cleaned.name;
            const description = `<pre>${escapeHtml(JSON.stringify(cleaned, null, 2))}</pre>`;
            addTimelineEntry(state.canRealtime.timeline.decoded, {
                icon: 'ti-device-analytics',
                accentClass: 'bg-green',
                timeLabel: formatTimeLabel(frame.timestamp),
                title: `${summary} (${formatCanIdentifierDisplay(identifier)})`,
                descriptionHtml: description,
            });
            refreshCanCharts();
        }
    } catch (error) {
        console.warn('Invalid CAN payload', error);
    }
}

function escapeHtml(value) {
    return String(value).replace(/[&<>"]/g, (char) => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
    })[char]);
}

function connectWebSocket(path, onMessage) {
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${protocol}://${window.location.host}${path}`;
    const socket = new WebSocket(url);
    socket.addEventListener('message', onMessage);
    socket.addEventListener('close', () => {
        setTimeout(() => connectWebSocket(path, onMessage), 5000);
    });
    socket.addEventListener('error', () => socket.close());
    return socket;
}

function setupTabs() {
    document.querySelectorAll('.tab-button').forEach((button) => {
        button.addEventListener('click', () => {
            setActiveTab(button.dataset.tab);
        });
    });
}

function setupHistoryControls() {
    const sourceSelect = document.getElementById('history-source');
    const select = document.getElementById('history-range');
    const refresh = document.getElementById('history-refresh');
    const exportBtn = document.getElementById('history-export');
    const archiveSelect = document.getElementById('history-archive-file');
    const archiveDownload = document.getElementById('history-archive-download');
    const pageSizeSelect = document.getElementById('history-page-size');
    const pagination = document.getElementById('history-pagination');

    if (sourceSelect) {
        sourceSelect.addEventListener('change', () => {
            state.historySource = sourceSelect.value;
            state.historyPage = 1;
            updateArchiveControls();
            if (state.historySource === 'archive') {
                fetchHistoryArchives()
                    .then(() => fetchArchiveSamples(state.selectedArchive, state.historyLimit))
                    .catch((error) => console.error(error));
            } else {
                fetchLiveHistory(state.historyLimit).catch((error) => console.error(error));
            }
        });
    }

    if (select) {
        select.addEventListener('change', () => {
            state.historyLimit = Number(select.value);
            state.historyPage = 1;
            fetchHistory(state.historyLimit).catch((error) => console.error(error));
        });
    }

    if (refresh) {
        refresh.addEventListener('click', () => {
            if (state.historySource === 'archive') {
                fetchHistoryArchives()
                    .then(() => fetchArchiveSamples(state.selectedArchive, state.historyLimit))
                    .catch((error) => console.error(error));
            } else {
                fetchLiveHistory(state.historyLimit).catch((error) => console.error(error));
            }
        });
    }

    if (exportBtn) {
        exportBtn.addEventListener('click', () => {
            const rows = state.history.map((sample) => [
                sample.timestamp_iso ? sample.timestamp_iso : new Date(sample.timestamp).toISOString(),
                sample.pack_voltage,
                sample.pack_current,
                sample.state_of_charge,
                sample.average_temperature,
            ]);
            const header = 'timestamp,pack_voltage,pack_current,state_of_charge,average_temperature';
            const csv = [header, ...rows.map((row) => row.join(','))].join('\n');
            const blob = new Blob([csv], { type: 'text/csv' });
            const url = URL.createObjectURL(blob);
            const link = document.createElement('a');
            link.href = url;
            link.download = 'tinybms_history.csv';
            link.click();
            URL.revokeObjectURL(url);
        });
    }

    if (archiveSelect) {
        archiveSelect.addEventListener('change', () => {
            state.selectedArchive = archiveSelect.value || null;
            if (state.historySource === 'archive') {
                fetchArchiveSamples(state.selectedArchive, state.historyLimit).catch((error) =>
                    console.error(error)
                );
            }
        });
    }

    if (archiveDownload) {
        archiveDownload.addEventListener('click', () => {
            if (!state.selectedArchive) {
                return;
            }
            const url = `/api/history/download?file=${encodeURIComponent(state.selectedArchive)}`;
            window.open(url, '_blank');
        });
    }

    if (pageSizeSelect instanceof HTMLSelectElement) {
        pageSizeSelect.addEventListener('change', () => {
            const parsed = Number.parseInt(pageSizeSelect.value, 10);
            state.historyPageSize = Number.isFinite(parsed) && parsed > 0 ? parsed : 10;
            state.historyPage = 1;
            renderHistoryTable(state.history, { preservePage: false });
        });
    }

    if (pagination) {
        pagination.addEventListener('click', (event) => {
            const target = event.target instanceof Element ? event.target.closest('button[data-page]') : null;
            if (!target) {
                return;
            }
            event.preventDefault();
            const { page } = target.dataset;
            const pageCount = Math.max(1, Math.ceil(state.history.length / Math.max(state.historyPageSize || 10, 1)));

            if (page === 'prev') {
                if (state.historyPage > 1) {
                    state.historyPage -= 1;
                    renderHistoryTable(state.history, { preservePage: true });
                }
                return;
            }

            if (page === 'next') {
                if (state.historyPage < pageCount) {
                    state.historyPage += 1;
                    renderHistoryTable(state.history, { preservePage: true });
                }
                return;
            }

            const parsed = Number.parseInt(page || '', 10);
            if (!Number.isNaN(parsed) && parsed >= 1 && parsed <= pageCount) {
                state.historyPage = parsed;
                renderHistoryTable(state.history, { preservePage: true });
            }
        });
    }
}

function setElementText(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.textContent = value;
    }
}

function populateMqttForm(config) {
    if (!config) {
        return;
    }

    state.mqtt.lastConfig = config;
    const topics = config.topics || {};

    const setValue = (id, value) => {
        const input = document.getElementById(id);
        if (input) {
            input.value = value ?? '';
        }
    };

    setValue('mqtt-scheme', config.scheme || 'mqtt');
    setValue('mqtt-host', config.host || '');
    setValue('mqtt-port', config.port != null ? String(config.port) : '');
    setValue('mqtt-username', config.username || '');
    setValue('mqtt-password', config.password || '');
    setValue('mqtt-keepalive', config.keepalive != null ? String(config.keepalive) : '');
    setValue('mqtt-qos', config.default_qos != null ? String(config.default_qos) : '');

    const retainInput = document.getElementById('mqtt-retain');
    if (retainInput) {
        retainInput.checked = Boolean(config.retain);
    }

    setValue('mqtt-status-topic', topics.status || '');
    setValue('mqtt-metrics-topic', topics.metrics || '');
    setValue('mqtt-config-topic', topics.config || '');
    setValue('mqtt-can-raw-topic', topics.can_raw || '');
    setValue('mqtt-can-decoded-topic', topics.can_decoded || '');
    setValue('mqtt-can-ready-topic', topics.can_ready || '');

    displayMqttMessage('');
}

function displayMqttMessage(message, isError = false) {
    const element = document.getElementById('mqtt-config-message');
    if (!element) {
        return;
    }
    element.textContent = message;
    element.classList.remove('text-danger', 'text-success');
    if (!message) {
        return;
    }
    element.classList.add(isError ? 'text-danger' : 'text-success');
}

function extractMqttMessageBreakdown(status) {
    if (!status || typeof status !== 'object') {
        return [];
    }

    const entries = [];
    const addEntry = (rawLabel, value) => {
        const numeric = Number(value);
        if (!Number.isFinite(numeric) || numeric < 0) {
            return;
        }
        const label = String(rawLabel || 'Autre').trim();
        if (!label) {
            return;
        }
        entries.push({ label, value: numeric });
    };

    const topicCounts = status.topic_counts || status.topics?.counts;
    if (Array.isArray(topicCounts)) {
        topicCounts.forEach((entry) => {
            if (entry && typeof entry === 'object') {
                const label = entry.topic || entry.name || entry.label;
                const value = entry.count ?? entry.value ?? entry.messages;
                addEntry(label, value);
            }
        });
    } else if (topicCounts && typeof topicCounts === 'object') {
        Object.entries(topicCounts).forEach(([key, value]) => addEntry(key, value));
    }

    const messageCounts = status.message_counts || status.message_totals || status.messages;
    if (messageCounts && typeof messageCounts === 'object') {
        Object.entries(messageCounts).forEach(([key, value]) => {
            const normalized = key.replace(/_/g, ' ').trim();
            const label = normalized ? `${normalized.charAt(0).toUpperCase()}${normalized.slice(1)}` : key;
            addEntry(label, value);
        });
    }

    const fallbackFields = [
        { key: 'published_messages', label: 'Publiés' },
        { key: 'received_messages', label: 'Reçus' },
        { key: 'retained_messages', label: 'Retenus' },
        { key: 'dropped_messages', label: 'Perdus' },
        { key: 'queued_messages', label: 'En file' },
        { key: 'processed_messages', label: 'Traités' },
        { key: 'sent_messages', label: 'Envoyés' },
        { key: 'incoming_messages', label: 'Entrants' },
    ];
    fallbackFields.forEach(({ key, label }) => {
        if (Object.prototype.hasOwnProperty.call(status, key)) {
            addEntry(label, status[key]);
        }
    });

    const merged = new Map();
    entries.forEach((entry) => {
        const existing = merged.get(entry.label);
        if (existing) {
            existing.value += entry.value;
        } else {
            merged.set(entry.label, { ...entry });
        }
    });

    return Array.from(merged.values()).sort((a, b) => b.value - a.value);
}

function updateMqttMessageChart(status) {
    if (!state.mqtt.messageChart) {
        return;
    }
    const breakdown = status ? extractMqttMessageBreakdown(status) : [];
    state.mqtt.messageChart.setData(breakdown);
}

function buildMqttPayload(form) {
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
    if (!Number.isNaN(keepalive)) {
        payload.keepalive = keepalive;
    }

    const qos = Number.parseInt(form.default_qos?.value, 10);
    if (!Number.isNaN(qos)) {
        payload.default_qos = qos;
    }

    return payload;
}

async function fetchMqttConfig() {
    const response = await fetch('/api/mqtt/config', { cache: 'no-store' });
    if (!response.ok) {
        throw new Error('MQTT config request failed');
    }
    const config = await response.json();
    populateMqttForm(config);
}

async function fetchMqttStatus() {
    const response = await fetch('/api/mqtt/status', { cache: 'no-store' });
    if (!response.ok) {
        throw new Error('MQTT status request failed');
    }
    const status = await response.json();
    updateMqttStatus(status);
}

function updateMqttStatus(status, error) {
    updateMqttMessageChart(status);

    const badge = document.getElementById('mqtt-connection-state');
    const helper = document.getElementById('mqtt-last-error');
    if (!badge || !helper) {
        return;
    }

    badge.className = 'status-badge';
    helper.classList.remove('text-danger');

    const resetFields = () => {
        setElementText('mqtt-client-started', '--');
        setElementText('mqtt-wifi-state', '--');
        setElementText('mqtt-reconnect-count', '--');
        setElementText('mqtt-disconnect-count', '--');
        setElementText('mqtt-error-count', '--');
        setElementText('mqtt-last-event', '--');
        setElementText('mqtt-last-event-time', '--');
    };

    if (error) {
        resetFields();
        badge.textContent = 'Erreur';
        badge.classList.add('status-badge--error');
        const message = error instanceof Error ? error.message : 'Statut indisponible';
        helper.textContent = message;
        helper.classList.add('text-danger');
        return;
    }

    if (!status) {
        resetFields();
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

    const lastError = status.last_error || '';
    if (lastError) {
        helper.textContent = lastError;
        helper.classList.add('text-danger');
    } else {
        helper.textContent = 'Aucune erreur récente';
    }

    setElementText('mqtt-client-started', status.client_started ? 'Actif' : 'Arrêté');
    setElementText('mqtt-wifi-state', status.wifi_connected ? 'Connecté' : 'Déconnecté');
    setElementText('mqtt-reconnect-count', String(status.reconnects ?? 0));
    setElementText('mqtt-disconnect-count', String(status.disconnects ?? 0));
    setElementText('mqtt-error-count', String(status.errors ?? 0));
    setElementText('mqtt-last-event', status.last_event || '--');

    const timestamp = Number(status.last_event_timestamp_ms);
    if (Number.isFinite(timestamp) && timestamp > 0) {
        setElementText('mqtt-last-event-time', new Date(timestamp).toLocaleString());
    } else {
        setElementText('mqtt-last-event-time', '--');
    }
}

function refreshMqttData(forceConfig = false) {
    fetchMqttStatus().catch((statusError) => {
        console.error('Failed to refresh MQTT status', statusError);
        updateMqttStatus(null, statusError);
    });

    if (forceConfig || state.mqtt.lastConfig == null) {
        fetchMqttConfig().catch((configError) => {
            console.error('Failed to refresh MQTT config', configError);
            displayMqttMessage('Impossible de charger la configuration MQTT.', true);
        });
    }
}

function startMqttStatusPolling() {
    if (state.mqtt.statusInterval != null) {
        return;
    }
    state.mqtt.statusInterval = window.setInterval(() => {
        fetchMqttStatus().catch((error) => {
            console.error('MQTT status poll failed', error);
            updateMqttStatus(null, error);
        });
    }, MQTT_STATUS_POLL_INTERVAL_MS);
}

function stopMqttStatusPolling() {
    if (state.mqtt.statusInterval != null) {
        window.clearInterval(state.mqtt.statusInterval);
        state.mqtt.statusInterval = null;
    }
}

async function handleMqttSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    if (!(form instanceof HTMLFormElement)) {
        return;
    }

    const submitButton = form.querySelector('button[type="submit"]');
    if (submitButton) {
        submitButton.disabled = true;
    }

    displayMqttMessage('Enregistrement de la configuration…');

    try {
        const payload = buildMqttPayload(form);
        const response = await fetch('/api/mqtt/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (!response.ok) {
            const errorText = (await response.text()) || 'MQTT config update failed';
            throw new Error(errorText);
        }

        await Promise.all([
            fetchMqttConfig(),
            fetchMqttStatus().catch((error) => {
                console.error('Failed to refresh MQTT status after update', error);
                updateMqttStatus(null, error);
            }),
        ]);

        displayMqttMessage('Configuration MQTT mise à jour.', false);
    } catch (error) {
        console.error('MQTT configuration update failed', error);
        const message = error instanceof Error ? error.message : 'Échec de la mise à jour MQTT';
        displayMqttMessage(`Échec de l’enregistrement: ${message}`, true);
    } finally {
        if (submitButton) {
            submitButton.disabled = false;
        }
    }
}

function setupMqttTab() {
    const form = document.getElementById('mqtt-config-form');
    if (form instanceof HTMLFormElement) {
        form.addEventListener('submit', handleMqttSubmit);
    }

    const refreshButton = document.getElementById('mqtt-refresh');
    if (refreshButton instanceof HTMLButtonElement) {
        refreshButton.addEventListener('click', () => {
            refreshMqttData(true);
        });
    }

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            stopMqttStatusPolling();
        } else {
            refreshMqttData();
            startMqttStatusPolling();
        }
    });
}

async function initialise() {
    setupTabs();
    setupHistoryControls();
    updateArchiveControls();
    setupMqttTab();
    setupConfigTab();
    const pageSizeSelect = document.getElementById('history-page-size');
    if (pageSizeSelect instanceof HTMLSelectElement) {
        const parsed = Number.parseInt(pageSizeSelect.value, 10);
        if (Number.isFinite(parsed) && parsed > 0) {
            state.historyPageSize = parsed;
        }
    }
    state.historyChart = new HistoryChart(document.getElementById('history-chart'));
    renderHistoryTable(state.history, { preservePage: false });
    const mqttChartElement = document.getElementById('mqtt-messages-chart');
    if (mqttChartElement) {
        state.mqtt.messageChart = new MqttMessageChart(mqttChartElement);
    }
    state.batteryCharts = new BatteryRealtimeCharts({
        gaugeElement: document.getElementById('battery-soc-gauge'),
        sparklineElement: document.getElementById('battery-pack-sparkline'),
        cellChartElement: document.getElementById('battery-cell-chart'),
    });
    state.uartRealtime.timeline.raw = document.getElementById('uart-timeline-raw');
    state.uartRealtime.timeline.decoded = document.getElementById('uart-timeline-decoded');
    const uartChartElement = document.getElementById('uart-frames-chart');
    if (uartChartElement) {
        state.uartRealtime.charts = new UartCharts({
            distributionElement: uartChartElement,
        });
    }
    state.canRealtime.timeline.raw = document.getElementById('can-timeline-raw');
    state.canRealtime.timeline.decoded = document.getElementById('can-timeline-decoded');
    const canHeatmapElement = document.getElementById('can-heatmap-chart');
    const canThroughputElement = document.getElementById('can-throughput-chart');
    if (canHeatmapElement || canThroughputElement) {
        state.canRealtime.charts = new CanCharts({
            heatmapElement: canHeatmapElement,
            throughputElement: canThroughputElement,
        });
    }
    setupRealtimeViewControls();
    setupCanFilters();

    try {
        await Promise.all([
            fetchStatus(),
            fetchLiveHistory(state.historyLimit),
            fetchRegisters(),
            fetchConfig().catch(() => {
                // Errors already logged and surfaced via updateConfigStatus
            }),
            fetchMqttConfig().catch((error) => {
                console.error('Failed to load MQTT config', error);
                displayMqttMessage('Impossible de charger la configuration MQTT.', true);
            }),
            fetchMqttStatus().catch((error) => {
                console.error('Failed to load MQTT status', error);
                updateMqttStatus(null, error);
            }),
        ]);
    } catch (error) {
        console.error('Initialisation failed', error);
    }

    fetchHistoryArchives()
        .catch((error) => {
            console.error('Failed to load history archives', error);
            updateArchiveControls();
        })
        .finally(() => {
            updateArchiveControls();
        });

    startMqttStatusPolling();

    connectWebSocket('/ws/telemetry', handleTelemetryMessage);
    connectWebSocket('/ws/events', handleEventMessage);
    connectWebSocket('/ws/uart', handleUartMessage);
    connectWebSocket('/ws/can', handleCanMessage);
}

window.addEventListener('beforeunload', stopMqttStatusPolling);
window.addEventListener('DOMContentLoaded', initialise);
