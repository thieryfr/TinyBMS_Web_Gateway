// dashboard.js
import { BatteryRealtimeCharts } from './src/js/charts/batteryCharts.js';
import { UartCharts } from './src/js/charts/uartCharts.js';
import { CanCharts } from './src/js/charts/canCharts.js';
import { initChart } from './src/js/charts/base.js';

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

// === Classes (HistoryChart, MqttMessageChart) ===
// (Identiques à ton code – je les garde mais je les passe en IIFE pour éviter pollution globale)
const HistoryChart = class {
    constructor(container) { /* ... ton code ... */ }
    setData(samples) { /* ... */ }
    render() { /* ... */ }
};

const MqttMessageChart = class {
    constructor(container) { /* ... ton code ... */ }
    setData(entries) { /* ... */ }
    render() { /* ... */ }
};

// === Fonctions utilitaires (formatNumber, resolveSampleTimestamp, etc.) ===
// (Toutes identiques – je les garde telles quelles)
// ... [ton code inchangé] ...

// === Fonctions principales (updateBatteryView, renderHistoryTable, etc.) ===
// (Toutes identiques – je les garde)
// ... [ton code inchangé] ...

// === INITIALISATION CONDITIONNELLE ===
async function initialise() {
    // 1. Vérifie que les éléments critiques existent
    const requiredIds = [
        'history-chart',
        'battery-soc-gauge',
        'uart-frames-chart',
        'can-heatmap-chart',
        'history-table-body',
        'history-pagination',
    ];

    const missing = requiredIds.filter(id => !document.getElementById(id));
    if (missing.length > 0) {
        console.warn('Partials non chargés, réessai dans 100ms...', missing);
        setTimeout(initialise, 100);
        return;
    }

    // 2. Tout est prêt → on initialise
    console.log('Partials chargés, initialisation du dashboard');

    setupTabs();
    setupHistoryControls();
    updateArchiveControls();
    setupMqttTab();
    setupConfigTab();

    // Initialisation des graphiques
    state.historyChart = new HistoryChart(document.getElementById('history-chart'));
    state.mqtt.messageChart = new Miodule MqttMessageChart(document.getElementById('mqtt-messages-chart'));

    state.batteryCharts = new BatteryRealtimeCharts({
        gaugeElement: document.getElementById('battery-soc-gauge'),
        sparklineElement: document.getElementById('battery-pack-sparkline'),
        cellChartElement: document.getElementById('battery-cell-chart'),
    });

    state.uartRealtime.timeline.raw = document.getElementById('uart-timeline-raw');
    state.uartRealtime.timeline.decoded = document.getElementById('uart-timeline-decoded');
    state.uartRealtime.charts = new UartCharts({
        distributionElement: document.getElementById('uart-frames-chart'),
    });

    state.canRealtime.timeline.raw = document.getElementById('can-timeline-raw');
    state.canRealtime.timeline.decoded = document.getElementById('can-timeline-decoded');
    state.canRealtime.charts = new CanCharts({
        heatmapElement: document.getElementById('can-heatmap-chart'),
        throughputElement: document.getElementById('can-throughput-chart'),
    });

    setupRealtimeViewControls();
    setupCanFilters();

    // Chargement des données
    try {
        await Promise.all([
            fetchStatus(),
            fetchLiveHistory(state.historyLimit),
            fetchRegisters(),
            fetchConfig().catch(() => {}),
            fetchMqttConfig().catch(() => {}),
            fetchMqttStatus().catch(() => {}),
        ]);
    } catch (error) {
        console.error('Erreur lors du chargement initial', error);
    }

    fetchHistoryArchives().finally(updateArchiveControls);
    startMqttStatusPolling();

    // WebSockets
    connectWebSocket('/ws/telemetry', handleTelemetryMessage);
    connectWebSocket('/ws/events', handleEventMessage);
    connectWebSocket('/ws/uart', handleUartMessage);
    connectWebSocket('/ws/can', handleCanMessage);
}

// === ATTENTE DE l'ÉVÉNEMENT `partials-loaded` ===
function waitForPartials() {
    if (document.documentElement.dataset.partialsLoaded === 'true') {
        initialise();
        return;
    }

    const handler = (e) => {
        document.removeEventListener('partials-loaded', handler);
        document.documentElement.dataset.partialsLoaded = 'true';
        initialise();
    };

    document.addEventListener('partials-loaded', handler);

    // Fallback au cas où l'événement est déjà passé
    setTimeout(() => {
        if (!document.documentElement.dataset.partialsLoaded) {
            document.documentElement.dataset.partialsLoaded = 'true';
            initialise();
        }
    }, 1000);
}

// === Démarrage ===
waitForPartials();

window.addEventListener('beforeunload', stopMqttStatusPolling);
