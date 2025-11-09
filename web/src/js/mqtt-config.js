const MQTT_CONFIG_ENDPOINT = '/api/mqtt/config';
const MQTT_STATUS_ENDPOINT = '/api/mqtt/status';
const MQTT_TEST_ENDPOINT = '/api/mqtt/test';
const DEFAULT_NETWORK_ERROR_MESSAGE = 'Impossible de contacter le serveur.';
const SYSTEM_RUNTIME_ENDPOINTS = ['/api/monitoring/runtime', '/api/metrics/runtime'];
const MQTT_SUCCESS_EVENTS = new Set(['connected', 'published', 'subscribed', 'data']);

const NUMERIC_VALIDATION_RULES = {
  port: {
    min: 1,
    max: 65535,
    allowEmpty: false,
    message: 'Le port MQTT doit être compris entre 1 et 65535.',
  },
  keepalive: {
    min: 0,
    allowEmpty: true,
    message: 'Le keepalive doit être un entier supérieur ou égal à 0.',
  },
  default_qos: {
    min: 0,
    max: 2,
    allowEmpty: true,
    message: 'Le QoS par défaut doit être compris entre 0 et 2.',
  },
};

const TOPIC_VALIDATION_RULES = {
  status_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/status$/,
    message: 'Le topic statut doit suivre le format bms/<identifiant>/status sans espaces.',
  },
  metrics_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/metrics$/,
    message: 'Le topic mesures doit suivre le format bms/<identifiant>/metrics sans espaces.',
  },
  config_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/config$/,
    message: 'Le topic configuration doit suivre le format bms/<identifiant>/config sans espaces.',
  },
  can_raw_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/can\/raw$/,
    message: 'Le topic CAN brut doit suivre le format bms/<identifiant>/can/raw sans espaces.',
  },
  can_decoded_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/can\/decoded$/,
    message: 'Le topic CAN décodé doit suivre le format bms/<identifiant>/can/decoded sans espaces.',
  },
  can_ready_topic: {
    pattern: /^bms\/[A-Za-z0-9._-]+\/can\/ready$/,
    message: 'Le topic CAN prêts doit suivre le format bms/<identifiant>/can/ready sans espaces.',
  },
};

let networkErrorBanner = null;
let networkErrorTimeoutId = null;

let originalSnapshot = null;

function formatDateTime(timestampMs) {
  if (!Number.isFinite(timestampMs) || timestampMs <= 0) {
    return null;
  }

  try {
    return new Date(timestampMs).toLocaleString('fr-FR');
  } catch (err) {
    return null;
  }
}

function updateFirmwareSummary(value) {
  const el = document.getElementById('mqtt-summary-firmware');
  if (!el) return;

  const text = typeof value === 'string' && value.trim().length > 0 ? value.trim() : '--';
  el.textContent = text;

  if (text === '--') {
    el.removeAttribute('title');
  } else {
    el.setAttribute('title', text);
  }
}

function getNetworkBannerContainer() {
  return document.querySelector('.page-body .container-xl') || document.body;
}

function ensureNetworkBanner() {
  if (networkErrorBanner) return networkErrorBanner;

  const container = getNetworkBannerContainer();
  if (!container) return null;

  const banner = document.createElement('div');
  banner.className = 'alert alert-danger alert-important d-flex align-items-center gap-2 d-none';
  banner.setAttribute('role', 'alert');
  banner.setAttribute('aria-live', 'assertive');
  banner.setAttribute('aria-atomic', 'true');

  const icon = document.createElement('i');
  icon.className = 'ti ti-plug-off';
  icon.setAttribute('aria-hidden', 'true');

  const message = document.createElement('div');
  message.className = 'flex-fill';
  message.dataset.networkMessage = 'true';

  const close = document.createElement('button');
  close.type = 'button';
  close.className = 'btn-close';
  close.setAttribute('aria-label', 'Fermer l’alerte réseau');
  close.addEventListener('click', () => {
    banner.classList.add('d-none');
    if (networkErrorTimeoutId) {
      window.clearTimeout(networkErrorTimeoutId);
      networkErrorTimeoutId = null;
    }
  });

  banner.append(icon, message, close);
  container.prepend(banner);
  networkErrorBanner = banner;
  return networkErrorBanner;
}

function showNetworkError(message) {
  const banner = ensureNetworkBanner();
  if (!banner) return;

  const container = banner.querySelector('[data-network-message]');
  if (container) {
    container.textContent = message || DEFAULT_NETWORK_ERROR_MESSAGE;
  }

  banner.classList.remove('d-none');

  if (networkErrorTimeoutId) window.clearTimeout(networkErrorTimeoutId);
  networkErrorTimeoutId = window.setTimeout(() => {
    banner.classList.add('d-none');
    networkErrorTimeoutId = null;
  }, 8000);
}

function clearNetworkError() {
  if (!networkErrorBanner) return;
  networkErrorBanner.classList.add('d-none');
  if (networkErrorTimeoutId) {
    window.clearTimeout(networkErrorTimeoutId);
    networkErrorTimeoutId = null;
  }
}

function handleNetworkError(err, fallbackMessage = DEFAULT_NETWORK_ERROR_MESSAGE) {
  if (err && typeof err === 'object') {
    if (err.__networkHandled) {
      return;
    }
    try {
      Object.defineProperty(err, '__networkHandled', {
        value: true,
        enumerable: false,
        configurable: true,
      });
    } catch (defineError) {
      err.__networkHandled = true; // eslint-disable-line no-param-reassign
    }
  }

  const details = typeof err?.message === 'string' && err.message.trim().length > 0 ? err.message.trim() : '';
  const message = details ? `${fallbackMessage} (${details})` : fallbackMessage;
  showNetworkError(message);
}

function escapeCssIdent(value) {
  if (typeof value !== 'string') return '';
  if (typeof CSS !== 'undefined' && typeof CSS.escape === 'function') {
    return CSS.escape(value);
  }
  return value.replace(/[^a-zA-Z0-9_-]/g, '\$&');
}

function getFeedbackElement(field, { create = false } = {}) {
  if (!field) return null;
  const ident = escapeCssIdent(field.name || field.id || '');
  let feedback = ident
    ? field.parentElement?.querySelector(`.invalid-feedback[data-feedback-for="${ident}"]`)
    : null;

  if (!feedback && ident) {
    const form = field.closest('form');
    if (form) {
      feedback = form.querySelector(`.invalid-feedback[data-feedback-for="${ident}"]`);
    }
  }

  if (!feedback && ident) {
    feedback = document.querySelector(`.invalid-feedback[data-feedback-for="${ident}"]`);
  }

  if (!feedback) {
    let sibling = field.nextElementSibling;
    while (sibling) {
      if (sibling.classList?.contains('invalid-feedback')) {
        feedback = sibling;
        break;
      }
      sibling = sibling.nextElementSibling;
    }
  }

  if (!feedback && create) {
    feedback = document.createElement('div');
    feedback.className = 'invalid-feedback';
    feedback.dataset.feedbackFor = field.name || field.id || '';
    feedback.setAttribute('aria-live', 'assertive');
    field.insertAdjacentElement('afterend', feedback);
  }

  return feedback || null;
}

function setFieldError(field, message) {
  if (!field) return false;
  const feedback = getFeedbackElement(field, { create: true });
  if (feedback) feedback.textContent = message || '';
  field.classList.add('is-invalid');
  return false;
}

function clearFieldError(field) {
  if (!field) return;
  const feedback = getFeedbackElement(field);
  if (feedback) feedback.textContent = '';
  field.classList.remove('is-invalid');
}

function validateNumberField(field, rule) {
  if (!field || !rule) return true;
  const value = field.value?.trim() ?? '';
  if (value === '') {
    if (rule.allowEmpty) {
      clearFieldError(field);
      return true;
    }
    return setFieldError(field, rule.message);
  }

  if (!/^-?\d+$/.test(value)) {
    return setFieldError(field, rule.message);
  }

  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed) || !Number.isInteger(parsed)) {
    return setFieldError(field, rule.message);
  }

  if (typeof rule.min === 'number' && parsed < rule.min) {
    return setFieldError(field, rule.message);
  }

  if (typeof rule.max === 'number' && parsed > rule.max) {
    return setFieldError(field, rule.message);
  }

  clearFieldError(field);
  return true;
}

function validateTopicField(field, rule) {
  if (!field || !rule) return true;
  const value = field.value?.trim() ?? '';
  if (value === '') {
    clearFieldError(field);
    return true;
  }

  if (!rule.pattern.test(value)) {
    return setFieldError(field, rule.message);
  }

  clearFieldError(field);
  return true;
}

function validateFieldByName(form, name) {
  if (!form || !name) return true;
  const field = form.elements.namedItem(name);
  if (!field) return true;

  if (Object.prototype.hasOwnProperty.call(NUMERIC_VALIDATION_RULES, name)) {
    return validateNumberField(field, NUMERIC_VALIDATION_RULES[name]);
  }

  if (Object.prototype.hasOwnProperty.call(TOPIC_VALIDATION_RULES, name)) {
    return validateTopicField(field, TOPIC_VALIDATION_RULES[name]);
  }

  return true;
}

function validateForm(form) {
  if (!form) return false;

  let valid = true;
  Object.keys(NUMERIC_VALIDATION_RULES).forEach((name) => {
    if (!validateFieldByName(form, name)) valid = false;
  });

  Object.keys(TOPIC_VALIDATION_RULES).forEach((name) => {
    if (!validateFieldByName(form, name)) valid = false;
  });

  if (!valid) {
    const firstInvalid = form.querySelector('.is-invalid');
    if (firstInvalid && typeof firstInvalid.focus === 'function') {
      try {
        firstInvalid.focus({ preventScroll: false });
      } catch (focusError) {
        firstInvalid.focus();
      }
    }
  }

  return valid;
}

function clearFormValidation() {
  const form = document.getElementById('mqtt-config-form');
  if (!form) return;
  form.querySelectorAll('.is-invalid').forEach((field) => {
    clearFieldError(field);
  });
}

function handleFieldValidationEvent(event) {
  const target = event.target;
  if (!target) return;
  const form = target.form;
  if (!form || form.id !== 'mqtt-config-form') return;

  const name = target.name || target.id;
  if (!name) return;

  if (target.classList.contains('is-invalid')) {
    validateFieldByName(form, name);
    return;
  }

  if (
    Object.prototype.hasOwnProperty.call(NUMERIC_VALIDATION_RULES, name) ||
    Object.prototype.hasOwnProperty.call(TOPIC_VALIDATION_RULES, name)
  ) {
    validateFieldByName(form, name);
  }
}

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
  clearFormValidation();
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
  try {
    const res = await fetch(MQTT_CONFIG_ENDPOINT, { cache: 'no-store' });
    if (!res.ok) {
      throw new Error('Config failed');
    }
    const config = await res.json();
    populateConfig(config);
    clearNetworkError();
    return config;
  } catch (err) {
    handleNetworkError(err, 'Impossible de charger la configuration MQTT.');
    throw err;
  }
}

function setStatusLoading(loading) {
  const containers = [];
  const body = document.getElementById('mqtt-status-body');
  if (body) {
    containers.push(body);
    body.setAttribute('aria-busy', loading ? 'true' : 'false');
  }

  const summary = document.getElementById('mqtt-status-summary');
  if (summary) {
    containers.push(summary);
    summary.setAttribute('aria-busy', loading ? 'true' : 'false');
  }

  if (!containers.length) return;

  containers.forEach((container) => {
    container.classList.toggle('placeholder-wave', loading);
  });

  const placeholders = [];
  containers.forEach((container) => {
    placeholders.push(...container.querySelectorAll('[data-placeholder]'));
  });

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
  const summaryBadge = document.getElementById('mqtt-summary-state');
  const lastSuccessEl = document.getElementById('mqtt-summary-last-success');

  if (!badge && !helper && !summaryBadge && !lastSuccessEl) return;

  const applyBadge = (element, modifier, text) => {
    if (!element) return;
    element.className = 'badge status-badge';
    if (modifier) {
      element.classList.add(modifier);
    }
    element.textContent = text;
  };

  const setLastSuccess = (timestampMs) => {
    if (!lastSuccessEl) return;

    const formatted = formatDateTime(Number(timestampMs));
    if (formatted) {
      lastSuccessEl.textContent = formatted;
      try {
        lastSuccessEl.setAttribute('title', new Date(Number(timestampMs)).toISOString());
      } catch (err) {
        lastSuccessEl.removeAttribute('title');
      }
    } else {
      lastSuccessEl.textContent = '--';
      lastSuccessEl.removeAttribute('title');
    }
  };

  if (helper) helper.classList.remove('text-danger');

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
    applyBadge(badge, 'status-badge--error', 'Erreur');
    applyBadge(summaryBadge, 'status-badge--error', 'Erreur');
    if (helper) {
      helper.textContent = error.message || 'Statut indisponible';
      helper.classList.add('text-danger');
    }
    setLastSuccess(null);
    return;
  }

  if (!status) {
    reset();
    applyBadge(badge, 'status-badge--disconnected', 'Inconnu');
    applyBadge(summaryBadge, 'status-badge--disconnected', 'Inconnu');
    if (helper) helper.textContent = 'Aucune donnée';
    setLastSuccess(null);
    return;
  }

  let modifier;
  let text;
  if (status.connected) {
    modifier = 'status-badge--connected';
    text = 'Connecté';
  } else if (status.client_started) {
    modifier = 'status-badge--disconnected';
    text = 'Déconnecté';
  } else {
    modifier = 'status-badge--error';
    text = 'Arrêté';
  }

  applyBadge(badge, modifier, text);
  applyBadge(summaryBadge, modifier, text);

  if (helper) {
    helper.textContent = status.last_error ? status.last_error : 'Aucune erreur récente';
    if (status.last_error) helper.classList.add('text-danger');
  }

  set('client-started', status.client_started ? 'Actif' : 'Arrêté');
  set('wifi-state', status.wifi_connected ? 'Connecté' : 'Déconnecté');
  set('reconnect-count', String(status.reconnects ?? 0));
  set('disconnect-count', String(status.disconnects ?? 0));
  set('error-count', String(status.errors ?? 0));
  set('last-event', status.last_event || '--');

  const ts = Number(status.last_event_timestamp_ms);
  const formattedEventTime = formatDateTime(ts);
  set('last-event-time', formattedEventTime ?? '--');

  let successTimestamp = Number(status.last_success_timestamp_ms);
  if (!Number.isFinite(successTimestamp) || successTimestamp <= 0) {
    successTimestamp = Number(status.last_publish_success_ms);
  }

  if (!Number.isFinite(successTimestamp) || successTimestamp <= 0) {
    const eventTimestamp = Number(status.last_event_timestamp_ms);
    const eventName = typeof status.last_event === 'string' ? status.last_event.toLowerCase() : '';
    if (Number.isFinite(eventTimestamp) && eventTimestamp > 0 && MQTT_SUCCESS_EVENTS.has(eventName)) {
      successTimestamp = eventTimestamp;
    }
  }

  setLastSuccess(Number.isFinite(successTimestamp) && successTimestamp > 0 ? successTimestamp : null);
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
    clearNetworkError();
  } catch (err) {
    handleNetworkError(err, 'Impossible de récupérer le statut MQTT.');
    updateMqttStatus(null, err);
  } finally {
    setStatusLoading(false);
    if (refresh) refresh.disabled = false;
  }
}

async function fetchRuntimeSummary() {
  const target = document.getElementById('mqtt-summary-firmware');
  if (!target) {
    return null;
  }

  updateFirmwareSummary('--');

  for (const endpoint of SYSTEM_RUNTIME_ENDPOINTS) {
    try {
      const response = await fetch(endpoint, { cache: 'no-store' });
      if (!response.ok) {
        continue;
      }

      const payload = await response.json();
      const firmware =
        typeof payload?.firmware === 'string'
          ? payload.firmware
          : typeof payload?.runtime?.firmware === 'string'
            ? payload.runtime.firmware
            : null;

      updateFirmwareSummary(firmware);
      return payload;
    } catch (err) {
      // Ignore and try the next endpoint
    }
  }

  updateFirmwareSummary(null);
  return null;
}

async function handleSubmit(e) {
  e.preventDefault();
  const form = e.currentTarget;
  const btn = form.querySelector('button[type="submit"]');

  if (!validateForm(form)) {
    displayMessage('Veuillez corriger les erreurs du formulaire.', true);
    return;
  }

  if (btn) btn.disabled = true;
  displayMessage('Enregistrement…');

  try {
    const payload = {
      scheme: form.scheme?.value || 'mqtt',
      host: form.host?.value?.trim() || '',
      port: Number.parseInt(form.port?.value, 10),
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

    await Promise.all([fetchMqttConfig(), fetchMqttStatus(), fetchRuntimeSummary()]);
    clearNetworkError();
    displayMessage('Configuration mise à jour avec succès !', false);
  } catch (err) {
    handleNetworkError(err, 'Échec de la mise à jour de la configuration MQTT.');
    const reason = typeof err?.message === 'string' && err.message.trim().length > 0 ? err.message.trim() : 'Erreur inconnue';
    displayMessage(`Échec: ${reason}`, true);
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
    handleNetworkError(err, 'Impossible de recharger la configuration MQTT.');
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
    clearNetworkError();

    if (!supported) {
      displayTestMessage(payload.message || 'Fonction non disponible.', true);
    } else if (ok) {
      displayTestMessage(payload.message || 'Connexion réussie.', false);
    } else {
      const message = payload.message || 'Impossible d’établir la connexion.';
      displayTestMessage(message, true);
    }
  } catch (err) {
    handleNetworkError(err, 'Impossible de tester la connexion MQTT.');
    const reason = typeof err?.message === 'string' && err.message.trim().length > 0 ? err.message.trim() : 'Erreur inconnue';
    displayTestMessage(`Erreur: ${reason}`, true);
  } finally {
    if (btn) btn.disabled = false;
  }
}

function setupEventListeners() {
  const form = document.getElementById('mqtt-config-form');
  if (form) {
    form.addEventListener('submit', handleSubmit);
    form.addEventListener('input', (event) => {
      handleFieldValidationEvent(event);
      window.requestAnimationFrame(updateDirtyStateFromForm);
    });
    form.addEventListener('change', (event) => {
      handleFieldValidationEvent(event);
      updateDirtyStateFromForm();
    });
  }

  const scheme = document.getElementById('mqtt-scheme');
  if (scheme) scheme.addEventListener('change', updateTlsVisibility);

  const refresh = document.getElementById('mqtt-refresh');
  if (refresh) {
    refresh.addEventListener('click', () => {
      fetchMqttStatus();
      fetchRuntimeSummary();
    });
  }

  const reset = document.getElementById('mqtt-reset');
  if (reset) reset.addEventListener('click', handleResetClick);

  const test = document.getElementById('mqtt-test');
  if (test) test.addEventListener('click', handleTestClick);
}

document.addEventListener('DOMContentLoaded', () => {
  setupPasswordToggle();
  updateTlsVisibility();
  setupEventListeners();

  const configPromise = fetchMqttConfig()
    .then(() => {
      displayMessage('');
    })
    .catch((error) => {
      displayMessage('Échec du chargement de la configuration.', true);
      return Promise.reject(error);
    });

  Promise.all([configPromise, fetchMqttStatus(), fetchRuntimeSummary()]).catch(() => {});
});

export {};
