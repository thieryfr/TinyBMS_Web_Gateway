const MQTT_STATUS_POLL_INTERVAL_MS = 5000;

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
    registers: new Map(),
    chart: null,
    mqtt: {
        statusInterval: null,
        lastStatus: null,
        lastConfig: null,
    },
};

class HistoryChart {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.samples = [];
    }

    setData(samples) {
        this.samples = samples.slice();
        this.render();
    }

    render() {
        const ctx = this.ctx;
        const { width, height } = this.canvas;
        ctx.clearRect(0, 0, width, height);

        if (this.samples.length === 0) {
            ctx.fillStyle = 'rgba(255,255,255,0.4)';
            ctx.font = '16px "Segoe UI"';
            ctx.fillText('Aucune donnée disponible', 20, height / 2);
            return;
        }

        const voltages = this.samples.map((s) => s.pack_voltage);
        const currents = this.samples.map((s) => s.pack_current);
        const timestamps = this.samples.map((s) => s.timestamp);

        const minV = Math.min(...voltages);
        const maxV = Math.max(...voltages);
        const minC = Math.min(...currents);
        const maxC = Math.max(...currents);
        const minT = Math.min(...timestamps);
        const maxT = Math.max(...timestamps);

        const pad = 40;
        ctx.fillStyle = 'rgba(255,255,255,0.08)';
        ctx.fillRect(pad, pad / 2, width - pad * 1.5, height - pad * 1.5);

        ctx.strokeStyle = 'rgba(255,255,255,0.15)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(pad, height - pad);
        ctx.lineTo(width - pad / 2, height - pad);
        ctx.moveTo(pad, pad / 2);
        ctx.lineTo(pad, height - pad);
        ctx.stroke();

        const project = (timestamp, value, minValue, maxValue) => {
            const x = pad + ((timestamp - minT) / Math.max(maxT - minT, 1)) * (width - pad * 1.5);
            const y = height - pad - ((value - minValue) / Math.max(maxValue - minValue, 1)) * (height - pad * 1.5);
            return [x, y];
        };

        ctx.lineWidth = 2.5;
        ctx.strokeStyle = 'rgba(0, 168, 150, 0.9)';
        ctx.beginPath();
        this.samples.forEach((sample, index) => {
            const [x, y] = project(sample.timestamp, sample.pack_voltage, minV, maxV);
            if (index === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();

        ctx.strokeStyle = 'rgba(255, 209, 102, 0.9)';
        ctx.beginPath();
        this.samples.forEach((sample, index) => {
            const [x, y] = project(sample.timestamp, sample.pack_current, minC, maxC);
            if (index === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();

        ctx.fillStyle = 'rgba(255,255,255,0.6)';
        ctx.font = '13px "Segoe UI"';
        ctx.fillText(`Tension min ${minV.toFixed(2)} V / max ${maxV.toFixed(2)} V`, pad + 8, pad + 12);
        ctx.fillText(`Courant min ${minC.toFixed(2)} A / max ${maxC.toFixed(2)} A`, pad + 8, pad + 28);
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

function updateBatteryView(data) {
    document.getElementById('battery-voltage').textContent = formatNumber(data.pack_voltage, 'V');
    document.getElementById('battery-current').textContent = formatNumber(data.pack_current, 'A');
    document.getElementById('battery-soc').textContent = formatNumber(data.state_of_charge, '%', 1);
    document.getElementById('battery-soh').textContent = `SOH ${formatNumber(data.state_of_health, '%', 1)}`;
    document.getElementById('battery-temperature').textContent = formatNumber(data.average_temperature, '°C', 1);
    document.getElementById('battery-temp-extra').textContent = `MOSFET: ${formatNumber(data.mos_temperature, '°C', 1)}`;
    document.getElementById('battery-minmax').textContent = `min ${data.min_cell_mv || '--'} mV • max ${data.max_cell_mv || '--'} mV`;
    document.getElementById('battery-balancing').textContent = `Équilibrage: 0x${(data.balancing_bits || 0).toString(16).toUpperCase()}`;

    const alarmList = document.getElementById('battery-alarms');
    const warningList = document.getElementById('battery-warnings');
    alarmList.innerHTML = '';
    warningList.innerHTML = '';

    if (data.alarm_bits) {
        alarmList.appendChild(createListItem(`0x${data.alarm_bits.toString(16).toUpperCase()}`));
    } else {
        alarmList.appendChild(createListItem('Aucune alarme active'));
    }

    if (data.warning_bits) {
        warningList.appendChild(createListItem(`0x${data.warning_bits.toString(16).toUpperCase()}`));
    } else {
        warningList.appendChild(createListItem('Aucun avertissement'));
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
}

function createListItem(text) {
    const li = document.createElement('li');
    li.textContent = text;
    return li;
}

function addDefinition(container, key, value) {
    const dt = document.createElement('dt');
    dt.textContent = key;
    const dd = document.createElement('dd');
    dd.textContent = value;
    container.append(dt, dd);
}

function appendToList(list, html, limit = 100) {
    const li = document.createElement('li');
    li.innerHTML = html;
    list.prepend(li);
    while (list.children.length > limit) {
        list.removeChild(list.lastElementChild);
    }
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
        updateHistory([]);
        return;
    }
    const params = new URLSearchParams({ file });
    if (limit && Number(limit) > 0) params.set('limit', String(limit));
    const response = await fetch(`/api/history/archive?${params.toString()}`);
    if (!response.ok) throw new Error('Archive request failed');
    const payload = await response.json();
    const samples = (payload.samples || []).map(normalizeSample);
    state.history = samples;
    updateHistory(samples);
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
    const directory = state.historyDirectory || '/history';

    if (status) {
        status.textContent = state.historyStorageReady ? 'Flash: disponible' : 'Flash: indisponible';
        status.classList.toggle('warning', !state.historyStorageReady);
    }

    if (controls) {
        controls.classList.toggle('active', state.historySource === 'archive');
    }

    if (rangeGroup) {
        rangeGroup.classList.toggle('disabled', state.historySource === 'archive');
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

function updateHistory(samples) {
    if (!state.chart) {
        state.chart = new HistoryChart(document.getElementById('history-chart'));
    }
    state.chart.setData(samples);

    const tbody = document.getElementById('history-table-body');
    tbody.innerHTML = '';
    samples.slice(-200).reverse().forEach((sample) => {
        const row = document.createElement('tr');
        const timestampText = sample.timestamp_iso
            ? new Date(sample.timestamp_iso).toLocaleString()
            : formatTimestamp(sample.timestamp);
        row.innerHTML = `
            <td>${timestampText}</td>
            <td>${formatNumber(sample.pack_voltage, 'V')}</td>
            <td>${formatNumber(sample.pack_current, 'A')}</td>
            <td>${formatNumber(sample.state_of_charge, '%', 1)}</td>
            <td>${formatNumber(sample.average_temperature, '°C', 1)}</td>
        `;
        tbody.appendChild(row);
    });
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
    const card = document.createElement('article');
    card.className = 'config-card';
    card.dataset.key = register.key;

    const title = document.createElement('h3');
    title.textContent = `${register.label}${register.unit ? ` (${register.unit})` : ''}`;

    const valueLabel = document.createElement('div');
    valueLabel.textContent = `Adresse 0x${register.address.toString(16).toUpperCase()} • Min ${register.min} • Max ${register.max}`;
    valueLabel.className = 'config-meta';

    const inputs = document.createElement('div');
    inputs.className = 'inputs';

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
    card.append(title, valueLabel, inputs);
    return card;
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

function updateConfigStatus(message, isError = false) {
    const status = document.getElementById('config-status');
    status.textContent = message;
    status.style.color = isError ? '#ff6b6b' : 'var(--text-secondary)';
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
        updateHistory(state.history);
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
        if (data.type === 'uart_raw') {
            appendToList(
                document.getElementById('uart-raw-list'),
                `<strong>${new Date(data.timestamp).toLocaleTimeString()}</strong> – ID ${data.length} octets<br><code>${data.data}</code>`
            );
        } else if (data.type === 'uart_decoded') {
            appendToList(
                document.getElementById('uart-decoded-list'),
                `<strong>${new Date(data.timestamp).toLocaleTimeString()}</strong><br>${escapeHtml(JSON.stringify(data, null, 2))}`
            );
        }
    } catch (error) {
        console.warn('Invalid UART payload', error);
    }
}

function handleCanMessage(event) {
    try {
        const data = JSON.parse(event.data);
        if (data.type === 'can_raw') {
            appendToList(
                document.getElementById('can-raw-list'),
                `<strong>${new Date(data.timestamp).toLocaleTimeString()}</strong> – ID ${data.id}<br><code>${data.data}</code>`
            );
        } else if (data.type === 'can_decoded') {
            appendToList(
                document.getElementById('can-decoded-list'),
                `<strong>${new Date(data.timestamp).toLocaleTimeString()}</strong> – ${escapeHtml(data.description || '')}<br>${escapeHtml(JSON.stringify(data.bytes))}`
            );
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

    if (sourceSelect) {
        sourceSelect.addEventListener('change', () => {
            state.historySource = sourceSelect.value;
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
    element.classList.toggle('error', Boolean(isError && message));
    element.classList.toggle('success', Boolean(!isError && message));
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
    const badge = document.getElementById('mqtt-connection-state');
    const helper = document.getElementById('mqtt-last-error');
    if (!badge || !helper) {
        return;
    }

    badge.className = 'status-badge';
    helper.classList.remove('error');

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
        badge.textContent = 'Inconnu';
        badge.classList.add('status-badge--error');
        helper.textContent = 'Statut indisponible';
        helper.classList.add('error');
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
        helper.classList.add('error');
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
    state.chart = new HistoryChart(document.getElementById('history-chart'));

    try {
        await Promise.all([
            fetchStatus(),
            fetchLiveHistory(state.historyLimit),
            fetchRegisters(),
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
