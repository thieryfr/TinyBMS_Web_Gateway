const MQTT_CONFIG_ENDPOINT = '/api/mqtt/config';
const MQTT_STATUS_ENDPOINT = '/api/mqtt/status';
const MQTT_TEST_ENDPOINT = '/api/mqtt/test';

let originalSnapshot = null;

function updateTlsVisibility() {
  const scheme = document.getElementById('mqtt-scheme');
  const isSecure = scheme && scheme.value === 'mqtts';
  const sections = document.querySelectorAll('[data-tls-field]');
  sections.forEach((node) => {
    node.classList.toggle('d-none', !isSecure);
    node.setAttribute('aria-hidden', (!isSecure).toString());
  });
}

function setupPasswordToggle() {
  const input = document.getElementById('mqtt-password');
  const toggle = document.getElementById('mqtt-password-toggle');
  if (!input || !toggle) return;

  const updateLabel = (visible) => {
    const text = visible ? 'Masquer le mot de passe' : 'Afficher le mot de passe';
    toggle.setAttribute('aria-label', text);
    toggle.setAttribute('aria-pressed', visible.toString());
    const helper = toggle.querySelector('.password-toggle-label');
    if (helper) helper.textContent = text;
    const icon = toggle.querySelector('i');
    if (icon) {
      icon.classList.toggle('ti-eye', !visible);
      icon.classList.toggle('ti-eye-off', visible);
    }
  };

  toggle.addEventListener('click', () => {
    const isPassword = input.type === 'password';
    input.type = isPassword ? 'text' : 'password';
    updateLabel(isPassword);
  });

  updateLabel(false);
}

function displayMessage(msg, error = false) {
  const el = document.getElementById('mqtt-config-message');
  if (!el) return;
  el.textContent = msg;
  const hasMessage = Boolean(msg);
  el.classList.toggle('text-danger', error && hasMessage);
  el.classList.toggle('text-success', !error && hasMessage);
  if (!hasMessage) {
    el.classList.remove('text-danger', 'text-success');
  }
}

function displayTestMessage(msg, error = false) {
  const el = document.getElementById('mqtt-test-message');
  if (!el) return;
  el.textContent = msg;
  const hasMessage = Boolean(msg);
  el.classList.toggle('text-danger', error && hasMessage);
  el.classList.toggle('text-success', !error && hasMessage);
  el.classList.toggle('text-secondary', !hasMessage);
  if (!hasMessage) {
    el.classList.remove('text-danger', 'text-success');
  }
}

function getFormSnapshot() {
  const form = document.getElementById('mqtt-config-form');
  if (!form) return null;

  const getValue = (name, { trim = true } = {}) => {
    const field = form.elements.namedItem(name);
    if (!field) return '';
    const value = field.value ?? '';
    return trim ? value.trim() : value;
  };

  const getBool = (name) => {
    const field = form.elements.namedItem(name);
    if (!field) return false;
    return field.checked === true;
  };

  return {
    scheme: getValue('scheme'),
    host: getValue('host'),
    port: getValue('port'),
    username: getValue('username'),
    password: getValue('password', { trim: false }),
    client_cert_path: getValue('client_cert_path'),
    ca_cert_path: getValue('ca_cert_path'),
    verify_hostname: getBool('verify_hostname'),
    keepalive: getValue('keepalive'),
    default_qos: getValue('default_qos'),
    retain: getBool('retain'),
    status_topic: getValue('status_topic'),
    metrics_topic: getValue('metrics_topic'),
    config_topic: getValue('config_topic'),
    can_raw_topic: getValue('can_raw_topic'),
    can_decoded_topic: getValue('can_decoded_topic'),
    can_ready_topic: getValue('can_ready_topic'),
  };
}

function areSnapshotsEqual(a, b) {
  if (!a || !b) {
    return false;
  }
  return JSON.stringify(a) === JSON.stringify(b);
}

function setDirtyState(dirty) {
  const badge = document.getElementById('mqtt-unsaved-badge');
  if (!badge) return;
  badge.classList.toggle('d-none', !dirty);
}

function updateDirtyStateFromForm() {
  if (!originalSnapshot) {
    return;
  }
  const snapshot = getFormSnapshot();
  if (!snapshot) {
    setDirtyState(false);
    return;
  }
  setDirtyState(!areSnapshotsEqual(snapshot, originalSnapshot));
}

function populateConfig(config) {
  const setValue = (id, value) => {
    const el = document.getElementById(id);
    if (el) {
      el.value = value ?? '';
    }
  };

  const topics = config?.topics || {};

  setValue('mqtt-scheme', config?.scheme || 'mqtt');
  updateTlsVisibility();
  setValue('mqtt-host', config?.host || '');
  setValue('mqtt-port', config?.port != null ? String(config.port) : '');
  setValue('mqtt-username', config?.username || '');
  setValue('mqtt-password', config?.password || '');
  setValue('mqtt-client-cert', config?.client_cert_path || '');
  setValue('mqtt-ca-cert', config?.ca_cert_path || '');
  setValue('mqtt-keepalive', config?.keepalive != null ? String(config.keepalive) : '');
  setValue('mqtt-qos', config?.default_qos != null ? String(config.default_qos) : '');

  const retain = document.getElementById('mqtt-retain');
  if (retain) {
    retain.checked = Boolean(config?.retain);
  }

  const verify = document.getElementById('mqtt-verify-hostname');
  if (verify) {
    verify.checked = config?.verify_hostname !== false;
  }

  setValue('mqtt-status-topic', topics.status || '');
  setValue('mqtt-metrics-topic', topics.metrics || '');
  setValue('mqtt-config-topic', topics.config || '');
  setValue('mqtt-can-raw-topic', topics.can_raw || '');
  setValue('mqtt-can-decoded-topic', topics.can_decoded || '');
  setValue('mqtt-can-ready-topic', topics.can_ready || '');

  originalSnapshot = getFormSnapshot();
  setDirtyState(false);
}

async function fetchMqttConfig() {
  const res = await fetch(MQTT_CONFIG_ENDPOINT, { cache: 'no-store' });
  if (!res.ok) {
    throw new Error('Config failed');
  }
  const config = await res.json();
  populateConfig(config);
  return config;
}

function setStatusLoading(loading) {
  const body = document.getElementById('mqtt-status-body');
  if (!body) return;
  body.classList.toggle('placeholder-wave', loading);
  body.setAttribute('aria-busy', loading ? 'true' : 'false');

  const placeholders = body.querySelectorAll('[data-placeholder]');
  placeholders.forEach((el) => {
    if (loading) {
      if (!el.dataset.placeholderStored) {
        el.dataset.placeholderStored = '1';
        el.textContent = '';
      }
      el.classList.add('placeholder');
    } else {
      el.classList.remove('placeholder');
      if (el.dataset.placeholderStored) {
        delete el.dataset.placeholderStored;
      }
    }
  });
}

function updateMqttStatus(status, error) {
  const badge = document.getElementById('mqtt-connection-state');
  const helper = document.getElementById('mqtt-last-error');
  if (!badge || !helper) return;

  badge.className = 'badge status-badge';
  helper.classList.remove('text-danger');

  const set = (id, value) => {
    const el = document.getElementById(`mqtt-${id}`);
    if (el) el.textContent = value;
  };

  const reset = () => {
    [
      'client-started',
      'wifi-state',
      'reconnect-count',
      'disconnect-count',
      'error-count',
      'last-event',
      'last-event-time',
    ].forEach((id) => {
      set(id, '--');
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

  set('client-started', status.client_started ? 'Actif' : 'Arrêté');
  set('wifi-state', status.wifi_connected ? 'Connecté' : 'Déconnecté');
  set('reconnect-count', String(status.reconnects ?? 0));
  set('disconnect-count', String(status.disconnects ?? 0));
  set('error-count', String(status.errors ?? 0));
  set('last-event', status.last_event || '--');

  const ts = Number(status.last_event_timestamp_ms);
  set('last-event-time', Number.isFinite(ts) && ts > 0 ? new Date(ts).toLocaleString() : '--');
}

async function fetchMqttStatus() {
  const refresh = document.getElementById('mqtt-refresh');
  if (refresh) refresh.disabled = true;
  setStatusLoading(true);

  try {
    const res = await fetch(MQTT_STATUS_ENDPOINT, { cache: 'no-store' });
    if (!res.ok) throw new Error('Status failed');
    const status = await res.json();
    updateMqttStatus(status);
  } catch (err) {
    updateMqttStatus(null, err);
  } finally {
    setStatusLoading(false);
    if (refresh) refresh.disabled = false;
  }
}

async function handleSubmit(e) {
  e.preventDefault();
  const form = e.currentTarget;
  const btn = form.querySelector('button[type="submit"]');
  if (btn) btn.disabled = true;
  displayMessage('Enregistrement…');

  try {
    const payload = {
      scheme: form.scheme?.value || 'mqtt',
      host: form.host?.value?.trim() || '',
      port: Number.parseInt(form.port?.value, 10) || 0,
      username: form.username?.value?.trim() || '',
      password: form.password?.value || '',
      client_cert_path: document.getElementById('mqtt-client-cert')?.value?.trim() || '',
      ca_cert_path: document.getElementById('mqtt-ca-cert')?.value?.trim() || '',
      retain: form.retain?.checked || false,
      verify_hostname: document.getElementById('mqtt-verify-hostname')?.checked ?? true,
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

    const res = await fetch(MQTT_CONFIG_ENDPOINT, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    if (!res.ok) throw new Error((await res.text()) || 'Update failed');

    await Promise.all([fetchMqttConfig(), fetchMqttStatus()]);
    displayMessage('Configuration mise à jour avec succès !', false);
  } catch (err) {
    displayMessage(`Échec: ${err.message}`, true);
  } finally {
    if (btn) btn.disabled = false;
  }
}

async function handleResetClick(e) {
  const btn = e.currentTarget;
  if (btn) btn.disabled = true;
  displayMessage('Réinitialisation…');
  try {
    await fetchMqttConfig();
    displayMessage('Configuration rechargée.', false);
  } catch (err) {
    displayMessage('Échec du chargement de la configuration.', true);
  } finally {
    if (btn) btn.disabled = false;
  }
}

async function handleTestClick(e) {
  e.preventDefault();
  const btn = e.currentTarget;
  if (btn) btn.disabled = true;
  displayTestMessage('Test en cours…');

  try {
    const res = await fetch(MQTT_TEST_ENDPOINT, { cache: 'no-store' });
    const payload = await res.json().catch(() => ({ message: 'Réponse invalide', ok: false }));
    const ok = res.ok && payload.ok;
    const supported = payload.supported !== false;

    if (!supported) {
      displayTestMessage(payload.message || 'Fonction non disponible.', true);
    } else if (ok) {
      displayTestMessage(payload.message || 'Connexion réussie.', false);
    } else {
      const message = payload.message || 'Impossible d’établir la connexion.';
      displayTestMessage(message, true);
    }
  } catch (err) {
    displayTestMessage(`Erreur: ${err.message}`, true);
  } finally {
    if (btn) btn.disabled = false;
  }
}

function setupEventListeners() {
  const form = document.getElementById('mqtt-config-form');
  if (form) {
    form.addEventListener('submit', handleSubmit);
    form.addEventListener('input', () => {
      window.requestAnimationFrame(updateDirtyStateFromForm);
    });
    form.addEventListener('change', updateDirtyStateFromForm);
  }

  const scheme = document.getElementById('mqtt-scheme');
  if (scheme) scheme.addEventListener('change', updateTlsVisibility);

  const refresh = document.getElementById('mqtt-refresh');
  if (refresh) refresh.addEventListener('click', fetchMqttStatus);

  const reset = document.getElementById('mqtt-reset');
  if (reset) reset.addEventListener('click', handleResetClick);

  const test = document.getElementById('mqtt-test');
  if (test) test.addEventListener('click', handleTestClick);
}

document.addEventListener('DOMContentLoaded', () => {
  setupPasswordToggle();
  updateTlsVisibility();
  setupEventListeners();

  fetchMqttConfig()
    .then(() => displayMessage(''))
    .catch(() => displayMessage('Échec du chargement de la configuration.', true));

  fetchMqttStatus();
});

export {};
