const state = {
    telemetry: null,
    history: [],
    historyLimit: 120,
    registers: new Map(),
    chart: null,
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

async function fetchHistory(limit) {
    const params = new URLSearchParams();
    if (limit && Number(limit) > 0) params.set('limit', String(limit));
    const response = await fetch(`/api/history?${params.toString()}`);
    if (!response.ok) throw new Error('History request failed');
    const payload = await response.json();
    const samples = payload.samples || [];
    state.history = samples;
    updateHistory(samples);
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
        row.innerHTML = `
            <td>${formatTimestamp(sample.timestamp)}</td>
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
    state.history.push({
        timestamp: sample.timestamp,
        pack_voltage: sample.pack_voltage,
        pack_current: sample.pack_current,
        state_of_charge: sample.state_of_charge,
        state_of_health: sample.state_of_health,
        average_temperature: sample.average_temperature,
    });
    const limit = Number(state.historyLimit || 0);
    if (limit > 0 && state.history.length > limit) {
        state.history.splice(0, state.history.length - limit);
    }
    updateHistory(state.history);
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
    const select = document.getElementById('history-range');
    const refresh = document.getElementById('history-refresh');
    const exportBtn = document.getElementById('history-export');

    select.addEventListener('change', () => {
        state.historyLimit = Number(select.value);
        fetchHistory(state.historyLimit).catch((error) => console.error(error));
    });
    refresh.addEventListener('click', () => {
        fetchHistory(state.historyLimit).catch((error) => console.error(error));
    });
    exportBtn.addEventListener('click', () => {
        const rows = state.history.map((sample) => [
            new Date(sample.timestamp).toISOString(),
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

async function initialise() {
    setupTabs();
    setupHistoryControls();
    state.chart = new HistoryChart(document.getElementById('history-chart'));

    try {
        await Promise.all([fetchStatus(), fetchHistory(state.historyLimit), fetchRegisters()]);
    } catch (error) {
        console.error('Initialisation failed', error);
    }

    connectWebSocket('/ws/telemetry', handleTelemetryMessage);
    connectWebSocket('/ws/events', handleEventMessage);
    connectWebSocket('/ws/uart', handleUartMessage);
    connectWebSocket('/ws/can', handleCanMessage);
}

window.addEventListener('DOMContentLoaded', initialise);
