import tinyBMSConfig from '/assets/tiny/tinybms-config.js';

const elements = {
  portSelect: document.getElementById('serial-port'),
  baudRate: document.getElementById('baud-rate'),
  refreshPorts: document.getElementById('refresh-ports'),
  connect: document.getElementById('connect-port'),
  disconnect: document.getElementById('disconnect-port'),
  badge: document.getElementById('connection-badge'),
  details: document.getElementById('connection-details'),
  error: document.getElementById('connection-error'),
  configContainer: document.getElementById('tinybms-config-container'),
  configLoading: document.getElementById('config-loading-message'),
};

let tinyConfigLoaded = false;

async function fetchJSON(url, options = {}) {
  const response = await fetch(url, options);
  let payload = null;
  try {
    payload = await response.json();
  } catch (error) {
    payload = null;
  }
  if (!response.ok) {
    const message = payload?.error || response.statusText;
    throw new Error(message);
  }
  return payload;
}

function setButtonsState({ connected }) {
  elements.connect.disabled = connected;
  elements.disconnect.disabled = !connected;
  elements.portSelect.disabled = connected;
  elements.baudRate.disabled = connected;
}

function setStatus({ connected, port }) {
  if (connected) {
    elements.badge.className = 'badge bg-success';
    elements.badge.textContent = 'Connecté';
    elements.details.textContent = `${port.path} • ${port.baudRate} bauds`;
  } else {
    elements.badge.className = 'badge bg-secondary';
    elements.badge.textContent = 'Déconnecté';
    elements.details.textContent = 'Aucun périphérique connecté.';
  }
  setButtonsState({ connected });
}

function showError(message) {
  elements.error.textContent = message;
  elements.error.hidden = !message;
}

async function loadPorts() {
  try {
    showError('');
    elements.refreshPorts.disabled = true;
    const data = await fetchJSON('/api/ports');
    const { ports } = data;
    elements.portSelect.innerHTML = '';
    if (!ports || ports.length === 0) {
      const option = document.createElement('option');
      option.value = '';
      option.textContent = 'Aucun port détecté';
      elements.portSelect.appendChild(option);
      elements.portSelect.disabled = true;
      return;
    }
    ports.forEach((port) => {
      const option = document.createElement('option');
      option.value = port.path;
      option.textContent = port.path + (port.manufacturer ? ` • ${port.manufacturer}` : '');
      elements.portSelect.appendChild(option);
    });
    elements.portSelect.disabled = false;
  } catch (error) {
    showError(error.message);
  } finally {
    elements.refreshPorts.disabled = false;
  }
}

async function ensureTinyConfigLoaded() {
  if (tinyConfigLoaded) {
    elements.configContainer.hidden = false;
    return;
  }

  const src = elements.configContainer.dataset.src;
  if (!src) {
    throw new Error('Chemin du module TinyBMS introuvable');
  }

  const response = await fetch(src);
  if (!response.ok) {
    throw new Error(`Impossible de charger le module TinyBMS (${response.status})`);
  }
  const html = await response.text();
  elements.configContainer.innerHTML = html;
  elements.configContainer.hidden = false;
  await tinyBMSConfig.init();
  tinyConfigLoaded = true;
}

async function refreshRegisters() {
  if (!tinyConfigLoaded) {
    return;
  }
  elements.configLoading.hidden = false;
  try {
    await tinyBMSConfig.loadRegisters();
    await tinyBMSConfig.loadConfiguration();
  } catch (error) {
    showError(`Erreur lors du chargement des registres: ${error.message}`);
  } finally {
    elements.configLoading.hidden = true;
  }
}

async function connectPort() {
  try {
    showError('');
    const path = elements.portSelect.value;
    const baudRate = Number.parseInt(elements.baudRate.value, 10) || 115200;
    if (!path) {
      throw new Error('Veuillez sélectionner un port série.');
    }
    elements.connect.disabled = true;
    await fetchJSON('/api/connection/open', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path, baudRate }),
    });
    setStatus({ connected: true, port: { path, baudRate } });
    await ensureTinyConfigLoaded();
    await refreshRegisters();
  } catch (error) {
    showError(error.message);
    setStatus({ connected: false });
  } finally {
    elements.connect.disabled = false;
  }
}

async function disconnectPort() {
  try {
    showError('');
    elements.disconnect.disabled = true;
    await fetchJSON('/api/connection/close', { method: 'POST' });
    setStatus({ connected: false });
    elements.configContainer.hidden = true;
  } catch (error) {
    showError(error.message);
  } finally {
    elements.disconnect.disabled = false;
  }
}

async function refreshStatus() {
  try {
    const data = await fetchJSON('/api/connection/status');
    setStatus(data);
    if (data.connected) {
      await ensureTinyConfigLoaded();
      await refreshRegisters();
    } else {
      elements.configContainer.hidden = true;
    }
  } catch (error) {
    showError(error.message);
  }
}

elements.refreshPorts.addEventListener('click', (event) => {
  event.preventDefault();
  loadPorts();
});

elements.connect.addEventListener('click', (event) => {
  event.preventDefault();
  connectPort();
});

elements.disconnect.addEventListener('click', (event) => {
  event.preventDefault();
  disconnectPort();
});

window.addEventListener('DOMContentLoaded', async () => {
  await loadPorts();
  await refreshStatus();
});
