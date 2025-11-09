import { normalizeEventBusMetrics, coerceNumber } from './codeMetricsUtils.js';

const REFRESH_INTERVAL_MS = 10000;

const ENDPOINTS = {
    runtime: ['/api/monitoring/runtime', '/api/metrics/runtime'],
    eventBus: ['/api/monitoring/event-bus', '/api/event-bus/metrics'],
    tasks: ['/api/monitoring/tasks', '/api/system/tasks'],
    modules: ['/api/monitoring/modules', '/api/system/modules'],
};

const FALLBACK_DATA = {
    runtime: {
        event_loop: {},
    },
    eventBus: {
        dropped_by_consumer: [],
        queue_depth: [],
    },
    tasks: [],
    modules: [],
};

function deepClone(value) {
    return JSON.parse(JSON.stringify(value));
}

async function fetchJson(url) {
    try {
        const response = await fetch(url, {
            headers: { Accept: 'application/json' },
            cache: 'no-store',
        });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.warn(`[code-metrics] Impossible de récupérer ${url}:`, error);
        return null;
    }
}

async function fetchFromCandidates(candidates) {
    const urls = Array.isArray(candidates) ? candidates : [candidates];
    for (const url of urls) {
        if (!url) {
            continue;
        }
        const payload = await fetchJson(url);
        if (payload) {
            return { data: payload, source: url };
        }
    }
    return { data: null, source: urls.at(-1) ?? null };
}

function formatDuration(seconds) {
    if (!Number.isFinite(seconds)) return '--';
    const sec = Math.floor(seconds % 60);
    const minutes = Math.floor((seconds / 60) % 60);
    const hours = Math.floor((seconds / 3600) % 24);
    const days = Math.floor(seconds / 86400);

    const parts = [];
    if (days) parts.push(`${days} j`);
    if (hours || parts.length) parts.push(`${hours.toString().padStart(2, '0')} h`);
    parts.push(`${minutes.toString().padStart(2, '0')} min`);
    parts.push(`${sec.toString().padStart(2, '0')} s`);
    return parts.join(' ');
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return '--';
    const units = ['o', 'Ko', 'Mo', 'Go'];
    let value = bytes;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex += 1;
    }
    return `${value.toFixed(value < 10 && unitIndex > 0 ? 1 : 0)} ${units[unitIndex]}`;
}

function formatPercentage(value) {
    if (!Number.isFinite(value)) return '--';
    return `${value.toFixed(1)} %`;
}

function formatDateTime(isoString) {
    if (!isoString) return '--';
    const date = new Date(isoString);
    if (Number.isNaN(date.getTime())) return '--';
    return `${date.toLocaleDateString()} ${date.toLocaleTimeString()}`;
}

function computeHeapUsage(runtime) {
    const total = runtime?.total_heap_bytes ?? runtime?.heap_total_bytes ?? runtime?.heap_size_bytes;
    const free = runtime?.free_heap_bytes ?? runtime?.heap_free_bytes;
    const minFree = runtime?.min_free_heap_bytes ?? runtime?.heap_min_free_bytes;
    if (!Number.isFinite(free)) return { total: null, free: null, used: null, minFree: minFree ?? null };
    const used = Number.isFinite(total) ? total - free : null;
    return { total: Number.isFinite(total) ? total : null, free, used, minFree: Number.isFinite(minFree) ? minFree : null };
}

function computeCpuAverage(cpuLoad) {
    if (!cpuLoad) return null;
    const values = Object.values(cpuLoad).filter((value) => Number.isFinite(value));
    if (!values.length) return null;
    const sum = values.reduce((acc, value) => acc + value, 0);
    return sum / values.length;
}

class CodeMetricsDashboard {
    constructor() {
        this.refreshTimer = null;
        this.isRefreshing = false;
        this.history = {
            heap: [],
            eventLatency: [],
            cpuAvg: [],
        };
        this.maxHistoryPoints = 60;

        this.dom = {
            uptime: document.querySelector('#metric-uptime'),
            lastBoot: document.querySelector('#metric-last-boot'),
            cycleCount: document.querySelector('#metric-cycle-count'),
            resetReason: document.querySelector('#metric-reset-reason'),
            heap: document.querySelector('#metric-heap'),
            heapMin: document.querySelector('#metric-heap-min'),
            eventDrops: document.querySelector('#metric-event-drops'),
            eventBlocking: document.querySelector('#metric-event-blocking'),
            eventLatency: document.querySelector('#metric-event-latency'),
            eventLatencyChip: document.querySelector('#metric-event-latency-chip'),
            heapUsageChip: document.querySelector('#metric-heap-usage-chip'),
            cpuAvgChip: document.querySelector('#metric-cpu-avg'),
            taskCount: document.querySelector('#metric-task-count'),
            modeIndicator: document.querySelector('#metrics-mode-indicator'),
            sourceLabel: document.querySelector('#metrics-source-label'),
            lastRefresh: document.querySelector('#metrics-last-refresh'),
            tasksTable: document.querySelector('#tasks-table-body'),
            modulesTable: document.querySelector('#modules-table-body'),
            refreshButton: document.querySelector('#metrics-refresh'),
        };

        this.templates = {
            moduleRow: document.querySelector('#module-row-template'),
            taskRow: document.querySelector('#task-row-template'),
        };

        this.charts = {
            cpu: echarts.init(document.querySelector('#cpu-usage-chart')),
            eventLatency: echarts.init(document.querySelector('#event-loop-latency-chart')),
            heap: echarts.init(document.querySelector('#heap-usage-chart')),
            eventDrops: echarts.init(document.querySelector('#event-bus-drop-chart')),
            queues: echarts.init(document.querySelector('#queue-depth-chart')),
        };

        this.configureCharts();
        this.bindEvents();
    }

    bindEvents() {
        if (this.dom.refreshButton) {
            this.dom.refreshButton.addEventListener('click', () => this.update());
        }
        window.addEventListener('resize', () => {
            Object.values(this.charts).forEach((chart) => chart?.resize());
        });
    }

    configureCharts() {
        this.charts.cpu.setOption({
            tooltip: { formatter: '{a}<br />{b}: {c}%' },
            series: [
                {
                    name: 'CPU',
                    type: 'gauge',
                    min: 0,
                    max: 100,
                    progress: { show: true, roundCap: true, width: 10 },
                    detail: { valueAnimation: true, formatter: '{value}%' },
                    axisLine: { lineStyle: { width: 10 } },
                    splitLine: { length: 8 },
                    axisTick: { length: 6 },
                    axisLabel: { color: '#94a3b8', distance: 15 },
                    data: [{ value: 0, name: 'Charge moyenne' }],
                },
            ],
        });

        this.charts.eventLatency.setOption({
            tooltip: { trigger: 'axis', valueFormatter: (value) => `${value.toFixed(2)} ms` },
            legend: { data: ['Moyenne', 'Maximum'], top: 0 },
            grid: { left: 60, right: 20, top: 40, bottom: 40 },
            xAxis: { type: 'time', boundaryGap: false },
            yAxis: { type: 'value', name: 'ms', min: 0 },
            series: [
                { name: 'Moyenne', type: 'line', smooth: true, showSymbol: false, data: [] },
                { name: 'Maximum', type: 'line', smooth: true, showSymbol: false, data: [] },
            ],
        });

        this.charts.heap.setOption({
            tooltip: { trigger: 'axis' },
            legend: { data: ['Utilisée', 'Libre'], top: 0 },
            grid: { left: 60, right: 20, top: 40, bottom: 40 },
            xAxis: { type: 'time', boundaryGap: false },
            yAxis: { type: 'value', name: 'octets' },
            series: [
                { name: 'Utilisée', type: 'line', areaStyle: { opacity: 0.15 }, smooth: true, data: [] },
                { name: 'Libre', type: 'line', smooth: true, data: [] },
            ],
        });

        this.charts.eventDrops.setOption({
            tooltip: { trigger: 'axis' },
            legend: { data: ['Drops', 'Blocages'], top: 0 },
            grid: { left: 80, right: 20, top: 35, bottom: 40 },
            xAxis: { type: 'value', boundaryGap: [0, 0.01] },
            yAxis: { type: 'category', data: [] },
            series: [
                {
                    name: 'Drops',
                    type: 'bar',
                    stack: 'events',
                    data: [],
                    itemStyle: { borderRadius: [0, 6, 6, 0] },
                },
                {
                    name: 'Blocages',
                    type: 'bar',
                    stack: 'events',
                    data: [],
                    itemStyle: { borderRadius: [0, 6, 6, 0] },
                },
            ],
        });

        this.charts.queues.setOption({
            tooltip: { trigger: 'axis' },
            legend: { data: ['Utilisation', 'Capacité'], top: 0 },
            grid: { left: 60, right: 20, top: 40, bottom: 40 },
            xAxis: { type: 'category', data: [] },
            yAxis: { type: 'value', min: 0 },
            series: [
                { name: 'Utilisation', type: 'bar', data: [], itemStyle: { borderRadius: [6, 6, 0, 0] } },
                { name: 'Capacité', type: 'line', data: [], smooth: true },
            ],
        });
    }

    start() {
        this.update();
        if (this.refreshTimer) {
            clearInterval(this.refreshTimer);
        }
        this.refreshTimer = setInterval(() => this.update(), REFRESH_INTERVAL_MS);
    }

    async update() {
        if (this.isRefreshing) return;
        this.isRefreshing = true;
        this.dom.refreshButton?.classList.add('data-loading');

        const [runtime, eventBus, tasks, modules] = await Promise.all([
            fetchFromCandidates(ENDPOINTS.runtime),
            fetchFromCandidates(ENDPOINTS.eventBus),
            fetchFromCandidates(ENDPOINTS.tasks),
            fetchFromCandidates(ENDPOINTS.modules),
        ]);

        const fallback = deepClone(FALLBACK_DATA);
        const sources = {
            runtime: runtime.data != null,
            eventBus: eventBus.data != null,
            tasks: Array.isArray(tasks.data),
            modules: Array.isArray(modules.data),
        };
        const payload = {
            runtime: runtime.data ?? fallback.runtime,
            eventBus: eventBus.data ?? fallback.eventBus,
            tasks: Array.isArray(tasks.data) ? tasks.data : fallback.tasks,
            modules: Array.isArray(modules.data) ? modules.data : fallback.modules,
            isFallback: !sources.runtime && !sources.eventBus && !sources.tasks && !sources.modules,
            isPartialFallback:
                sources.runtime || sources.eventBus || sources.tasks || sources.modules
                    ? Object.values(sources).some((value) => !value)
                    : false,
        };

        this.render(payload);

        this.dom.refreshButton?.classList.remove('data-loading');
        this.isRefreshing = false;
    }

    render(payload) {
        this.renderRuntime(payload.runtime);
        this.renderEventBus(payload.eventBus);
        this.renderTasks(payload.tasks);
        this.renderModules(payload.modules);
        this.updateMeta(payload);
        this.updateModeIndicator(payload.isFallback, payload.isPartialFallback);
    }

    renderRuntime(runtime) {
        const uptime = runtime?.uptime_s;
        this.dom.uptime.textContent = formatDuration(uptime);
        this.dom.cycleCount.textContent = Number.isFinite(runtime?.cycle_count)
            ? runtime.cycle_count.toLocaleString('fr-FR')
            : '--';
        this.dom.resetReason.textContent = runtime?.reset_reason ?? '--';
        this.dom.lastBoot.textContent = runtime?.last_boot ? formatDateTime(runtime.last_boot) : '--';

        const heap = computeHeapUsage(runtime);
        this.dom.heap.textContent = formatBytes(heap.free);
        this.dom.heapMin.textContent = Number.isFinite(heap.minFree) ? formatBytes(heap.minFree) : '--';

        const cpuAverage = computeCpuAverage(runtime?.cpu_load);
        this.dom.cpuAvgChip.textContent = cpuAverage != null ? formatPercentage(cpuAverage) : '--';

        const drops = runtime?.event_loop?.dropped_total ?? runtime?.event_drops ?? null;
        const avgLatency = runtime?.event_loop?.avg_latency_ms;
        const maxLatency = runtime?.event_loop?.max_latency_ms;

        const totalDrops = Number.isFinite(drops) ? drops : null;
        this.dom.eventDrops.textContent = totalDrops != null ? totalDrops.toLocaleString('fr-FR') : '--';

        let latencyLabel = '--';
        if (avgLatency != null) {
            latencyLabel = maxLatency != null
                ? `${avgLatency.toFixed(2)} ms (max ${maxLatency.toFixed(2)} ms)`
                : `${avgLatency.toFixed(2)} ms`;
        }
        this.dom.eventLatency.textContent = latencyLabel;
        this.dom.eventLatencyChip.textContent = latencyLabel;

        const used = heap.used;
        if (cpuAverage != null) {
            this.charts.cpu.setOption({
                series: [
                    {
                        data: [{ value: Number(cpuAverage.toFixed(1)), name: 'Charge moyenne' }],
                    },
                ],
            });
        }

        const now = runtime?.timestamp_ms ? runtime.timestamp_ms : Date.now();

        if (avgLatency != null || maxLatency != null) {
            this.history.eventLatency.push({
                time: now,
                avg: avgLatency ?? null,
                max: maxLatency ?? null,
            });
        }

        if (heap.free != null || used != null) {
            this.history.heap.push({
                time: now,
                free: heap.free,
                used: used ?? null,
            });
        }

        if (cpuAverage != null) {
            this.history.cpuAvg.push({ time: now, value: cpuAverage });
        }

        this.trimHistory();
        this.renderRuntimeCharts();

        if (used != null && Number.isFinite(heap.total)) {
            const usagePercent = (used / heap.total) * 100;
            this.dom.heapUsageChip.textContent = `${usagePercent.toFixed(1)} % utilisé`;
        } else {
            this.dom.heapUsageChip.textContent = '--';
        }

    }

    renderRuntimeCharts() {
        const latencyAvgSeries = this.history.eventLatency
            .filter((point) => point.avg != null)
            .map((point) => [point.time, point.avg]);
        const latencyMaxSeries = this.history.eventLatency
            .filter((point) => point.max != null)
            .map((point) => [point.time, point.max]);

        this.charts.eventLatency.setOption({
            series: [
                { name: 'Moyenne', data: latencyAvgSeries },
                { name: 'Maximum', data: latencyMaxSeries },
            ],
        });

        const heapUsedSeries = this.history.heap
            .filter((point) => point.used != null)
            .map((point) => [point.time, point.used]);
        const heapFreeSeries = this.history.heap.map((point) => [point.time, point.free]);

        this.charts.heap.setOption({
            series: [
                { name: 'Utilisée', data: heapUsedSeries },
                { name: 'Libre', data: heapFreeSeries },
            ],
        });
    }

    renderEventBus(eventBus) {
        if (!eventBus) {
            if (this.dom.eventDrops) {
                this.dom.eventDrops.textContent = '--';
            }
            if (this.dom.eventBlocking) {
                this.dom.eventBlocking.textContent = '--';
            }
            this.charts.eventDrops.setOption({
                yAxis: { data: [] },
                series: [
                    { name: 'Drops', data: [] },
                    { name: 'Blocages', data: [] },
                ],
            });
            this.charts.queues.setOption({
                xAxis: { data: [] },
                series: [
                    { name: 'Utilisation', data: [] },
                    { name: 'Capacité', data: [] },
                ],
            });
            return;
        }

        const metrics = normalizeEventBusMetrics(eventBus);
        const totalDrops = metrics.droppedTotal;
        const totalBlocking = metrics.blockingTotal;

        if (this.dom.eventDrops) {
            this.dom.eventDrops.textContent = totalDrops != null ? totalDrops.toLocaleString('fr-FR') : '--';
        }
        if (this.dom.eventBlocking) {
            this.dom.eventBlocking.textContent =
                totalBlocking != null ? totalBlocking.toLocaleString('fr-FR') : '--';
        }

        this.charts.eventDrops.setOption({
            yAxis: { data: metrics.consumers.map((item) => item.name) },
            series: [
                {
                    name: 'Drops',
                    data: metrics.consumers.map((item) => item.dropped ?? 0),
                },
                {
                    name: 'Blocages',
                    data: metrics.consumers.map((item) => item.blocking ?? 0),
                },
            ],
        });

        const queues = metrics.queueDepth;
        this.charts.queues.setOption({
            xAxis: { data: queues.map((item) => item.name) },
            series: [
                { name: 'Utilisation', data: queues.map((item) => item.used ?? 0) },
                { name: 'Capacité', data: queues.map((item) => item.capacity ?? 0) },
            ],
        });
    }

    renderTasks(tasks) {
        if (!this.dom.tasksTable) return;
        this.dom.tasksTable.innerHTML = '';

        if (!Array.isArray(tasks) || !tasks.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 6;
            cell.className = 'text-center text-secondary';
            cell.textContent = 'Aucune tâche remontée';
            row.appendChild(cell);
            this.dom.tasksTable.appendChild(row);
            this.dom.taskCount.textContent = '0 tâche';
            return;
        }

        const sorted = tasks
            .slice()
            .sort((a, b) => (coerceNumber(b?.cpu_percent ?? b?.cpu ?? b?.cpu_usage) ?? 0) - (coerceNumber(a?.cpu_percent ?? a?.cpu ?? a?.cpu_usage) ?? 0));

        sorted.forEach((task) => {
            const row = this.templates.taskRow.content.firstElementChild.cloneNode(true);
            row.children[0].textContent = task.name ?? '--';
            row.children[1].innerHTML = this.renderTaskState(task.state);
            const cpuPercent = coerceNumber(task.cpu_percent ?? task.cpu ?? task.cpu_usage ?? task.cpu_load);
            row.children[2].textContent = cpuPercent != null ? formatPercentage(cpuPercent) : '--';
            const stackFree =
                coerceNumber(task.stack_high_water_mark ?? task.stack_free_bytes ?? task.stack_free) ?? null;
            row.children[3].textContent = stackFree != null ? `${stackFree} o` : '--';
            const coreId = task.core ?? task.core_id ?? task.affinity ?? '--';
            row.children[4].textContent = coreId;
            const runtimeTicks = coerceNumber(task.runtime_ticks ?? task.runtime ?? task.runtime_ms);
            row.children[5].textContent = runtimeTicks != null
                ? `${Math.round(runtimeTicks).toLocaleString('fr-FR')} ${task.runtime_ms != null ? 'ms' : 'ticks'}`
                : '--';
            this.dom.tasksTable.appendChild(row);
        });

        const label = sorted.length > 1 ? `${sorted.length} tâches` : '1 tâche';
        this.dom.taskCount.textContent = label;
    }

    renderTaskState(state) {
        if (!state) return '<span class="badge bg-secondary">--</span>';
        const normalized = String(state).toLowerCase();
        const map = {
            running: { label: 'En cours', className: 'bg-success' },
            ready: { label: 'Prête', className: 'bg-info' },
            blocked: { label: 'Bloquée', className: 'bg-warning' },
            suspended: { label: 'Suspendue', className: 'bg-secondary' },
            deleted: { label: 'Supprimée', className: 'bg-danger' },
        };
        const entry = map[normalized] ?? { label: state, className: 'bg-secondary' };
        return `<span class="badge ${entry.className}">${entry.label}</span>`;
    }

    renderModules(modules) {
        if (!this.dom.modulesTable) return;
        this.dom.modulesTable.innerHTML = '';

        if (!Array.isArray(modules) || !modules.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 4;
            cell.className = 'text-center text-secondary';
            cell.textContent = 'Aucun module surveillé';
            row.appendChild(cell);
            this.dom.modulesTable.appendChild(row);
            return;
        }

        modules.forEach((module) => {
            const row = this.templates.moduleRow.content.firstElementChild.cloneNode(true);
            row.children[0].textContent = module.name ?? '--';
            row.children[1].innerHTML = this.renderModuleStatus(module.status);
            row.children[2].textContent = module.detail ?? '--';
            row.children[3].textContent = module.last_event ? formatDateTime(module.last_event) : '--';
            this.dom.modulesTable.appendChild(row);
        });
    }

    renderModuleStatus(status) {
        if (!status) return '<span class="badge bg-secondary">Inconnu</span>';
        const normalized = String(status).toLowerCase();
        const map = {
            ok: { label: 'OK', className: 'bg-success' },
            warning: { label: 'Alerte', className: 'bg-warning' },
            error: { label: 'Erreur', className: 'bg-danger' },
            degraded: { label: 'Dégradé', className: 'bg-info' },
        };
        const entry = map[normalized] ?? { label: status, className: 'bg-secondary' };
        return `<span class="badge ${entry.className}">${entry.label}</span>`;
    }

    trimHistory() {
        const limitHistory = (array) => {
            while (array.length > this.maxHistoryPoints) {
                array.shift();
            }
        };
        limitHistory(this.history.heap);
        limitHistory(this.history.eventLatency);
        limitHistory(this.history.cpuAvg);
    }

    updateMeta(payload) {
        const now = new Date();
        this.dom.lastRefresh.textContent = `${now.toLocaleDateString()} ${now.toLocaleTimeString()}`;

        if (payload.isFallback) {
            this.dom.sourceLabel.textContent = 'Mode démo';
        } else if (payload.isPartialFallback) {
            this.dom.sourceLabel.textContent = 'Live (partiel)';
        } else {
            this.dom.sourceLabel.textContent = 'Live';
        }
    }

    updateModeIndicator(isFallback, isPartial) {
        if (!this.dom.modeIndicator) return;
        this.dom.modeIndicator.classList.toggle('live-indicator--fallback', Boolean(isFallback));
        this.dom.modeIndicator.classList.toggle('live-indicator--partial', Boolean(isPartial && !isFallback));
        const textSpan = this.dom.modeIndicator.querySelector('span:last-child');
        if (textSpan) {
            textSpan.textContent = isFallback
                ? 'Mode démo'
                : isPartial
                    ? 'Données partielles'
                    : 'Données en direct';
        }
    }
}

document.addEventListener('DOMContentLoaded', () => {
    const dashboard = new CodeMetricsDashboard();
    dashboard.start();
});
