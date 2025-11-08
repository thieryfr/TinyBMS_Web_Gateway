// Alert Management JavaScript Module
let alertsWebSocket = null;

// Connect to alerts WebSocket
function connectAlertsWebSocket() {
  const wsUrl = `ws://${window.location.hostname}/ws/alerts`;
  alertsWebSocket = new WebSocket(wsUrl);

  alertsWebSocket.onopen = () => {
    console.log('Alerts WebSocket connected');
  };

  alertsWebSocket.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.type === 'alerts' || data.type === 'alert') {
      // Handle real-time alert notification
      refreshActiveAlerts();
      refreshAlertStatistics();
      updateAlertBadge();
    }
  };

  alertsWebSocket.onerror = (error) => {
    console.error('Alerts WebSocket error:', error);
  };

  alertsWebSocket.onclose = () => {
    console.log('Alerts WebSocket closed, reconnecting...');
    setTimeout(connectAlertsWebSocket, 5000);
  };
}

// Fetch and display active alerts
async function refreshActiveAlerts() {
  try {
    const response = await fetch('/api/alerts/active');
    const data = await response.json();
    const alerts = data.alerts || data || [];

    const container = document.getElementById('active-alerts-container');
    if (!container) return;

    if (!alerts || alerts.length === 0) {
      container.innerHTML = '<div class="text-muted text-center py-4">Aucune alerte active</div>';
      return;
    }

    container.innerHTML = alerts.map(alert => `
      <div class="alert alert-${getSeverityClass(alert.severity)} alert-dismissible fade show mb-2" role="alert">
        <div class="d-flex">
          <div class="flex-grow-1">
            <h4 class="alert-title mb-1">${getAlertTitle(alert.type)}</h4>
            <div class="text-muted">${alert.message}</div>
            <div class="small text-muted mt-1">
              ${new Date(alert.timestamp_ms).toLocaleString('fr-FR')}
            </div>
          </div>
          <div class="ms-3">
            ${alert.status === 0 ? `
              <button type="button" class="btn btn-sm btn-primary" onclick="acknowledgeAlert(${alert.alert_id})">
                <i class="ti ti-check me-1"></i>
                Acquitter
              </button>
            ` : '<span class="badge bg-success">Acquitté</span>'}
          </div>
        </div>
      </div>
    `).join('');
  } catch (error) {
    console.error('Failed to fetch active alerts:', error);
  }
}

// Fetch and display alert history
async function refreshAlertHistory() {
  try {
    const response = await fetch('/api/alerts/history?limit=50');
    const data = await response.json();
    const alerts = data.alerts || data || [];

    const container = document.getElementById('alert-history-container');
    if (!container) return;

    if (!alerts || alerts.length === 0) {
      container.innerHTML = '<div class="text-muted text-center py-4">Aucun historique</div>';
      return;
    }

    container.innerHTML = '<div class="list-group list-group-flush">' + alerts.map(alert => `
      <div class="list-group-item">
        <div class="row align-items-center">
          <div class="col-auto">
            <span class="badge bg-${getSeverityClass(alert.severity)}">${getSeverityLabel(alert.severity)}</span>
          </div>
          <div class="col text-truncate">
            <div class="text-reset d-block">${alert.message}</div>
            <div class="d-block text-muted text-truncate mt-n1">
              ${new Date(alert.timestamp_ms).toLocaleString('fr-FR')}
            </div>
          </div>
        </div>
      </div>
    `).join('') + '</div>';
  } catch (error) {
    console.error('Failed to fetch alert history:', error);
  }
}

// Fetch and display alert statistics
async function refreshAlertStatistics() {
  try {
    const response = await fetch('/api/alerts/statistics');
    const stats = await response.json();

    const totalElem = document.getElementById('stat-total-alerts');
    const criticalElem = document.getElementById('stat-critical');
    const warningsElem = document.getElementById('stat-warnings');
    const infoElem = document.getElementById('stat-info');

    if (totalElem) totalElem.textContent = stats.total_alerts || 0;
    if (criticalElem) criticalElem.textContent = stats.critical_count || 0;
    if (warningsElem) warningsElem.textContent = stats.warning_count || 0;
    if (infoElem) infoElem.textContent = stats.info_count || 0;
  } catch (error) {
    console.error('Failed to fetch alert statistics:', error);
  }
}

// Update alert badge in navigation
async function updateAlertBadge() {
  try {
    const response = await fetch('/api/alerts/statistics');
    const stats = await response.json();

    const badge = document.getElementById('alert-count-badge');
    if (!badge) return;

    const activeCount = stats.active_alert_count || 0;

    if (activeCount > 0) {
      badge.textContent = activeCount;
      badge.classList.remove('d-none');
    } else {
      badge.classList.add('d-none');
    }
  } catch (error) {
    console.error('Failed to update alert badge:', error);
  }
}

// Acknowledge specific alert
async function acknowledgeAlert(alertId) {
  try {
    const response = await fetch(`/api/alerts/acknowledge/${alertId}`, {
      method: 'POST'
    });

    if (response.ok) {
      refreshActiveAlerts();
      refreshAlertStatistics();
      updateAlertBadge();
    }
  } catch (error) {
    console.error('Failed to acknowledge alert:', error);
  }
}

// Acknowledge all alerts
async function acknowledgeAllAlerts() {
  try {
    const response = await fetch('/api/alerts/acknowledge', {
      method: 'POST'
    });

    if (response.ok) {
      refreshActiveAlerts();
      refreshAlertStatistics();
      updateAlertBadge();
    }
  } catch (error) {
    console.error('Failed to acknowledge all alerts:', error);
  }
}

// Clear alert history
async function clearAlertHistory() {
  if (!confirm('Êtes-vous sûr de vouloir effacer tout l\'historique des alertes?')) {
    return;
  }

  try {
    const response = await fetch('/api/alerts/history', {
      method: 'DELETE'
    });

    if (response.ok) {
      refreshAlertHistory();
      refreshAlertStatistics();
    }
  } catch (error) {
    console.error('Failed to clear alert history:', error);
  }
}

// Load alert configuration
async function loadAlertConfig() {
  try {
    const response = await fetch('/api/alerts/config');
    const config = await response.json();

    document.getElementById('config-enabled').checked = config.enabled ?? true;
    document.getElementById('config-debounce').value = config.debounce_sec ?? 60;

    document.getElementById('config-temp-high-enabled').checked = config.temp_high_enabled ?? false;
    document.getElementById('config-temp-max').value = config.temperature_max_c ?? 45;

    document.getElementById('config-temp-low-enabled').checked = config.temp_low_enabled ?? false;
    document.getElementById('config-temp-min').value = config.temperature_min_c ?? 0;

    document.getElementById('config-cell-volt-high-enabled').checked = config.cell_volt_high_enabled ?? false;
    document.getElementById('config-cell-volt-max').value = config.cell_voltage_max_mv ?? 3650;

    document.getElementById('config-cell-volt-low-enabled').checked = config.cell_volt_low_enabled ?? false;
    document.getElementById('config-cell-volt-min').value = config.cell_voltage_min_mv ?? 2800;

    document.getElementById('config-monitor-events').checked = config.monitor_tinybms_events ?? true;
    document.getElementById('config-monitor-status').checked = config.monitor_status_changes ?? true;
  } catch (error) {
    console.error('Failed to load alert configuration:', error);
  }
}

// Save alert configuration
async function saveAlertConfig(event) {
  event.preventDefault();

  const config = {
    enabled: document.getElementById('config-enabled').checked,
    debounce_sec: parseInt(document.getElementById('config-debounce').value) || 60,

    temp_high_enabled: document.getElementById('config-temp-high-enabled').checked,
    temperature_max_c: parseFloat(document.getElementById('config-temp-max').value) || 45,

    temp_low_enabled: document.getElementById('config-temp-low-enabled').checked,
    temperature_min_c: parseFloat(document.getElementById('config-temp-min').value) || 0,

    cell_volt_high_enabled: document.getElementById('config-cell-volt-high-enabled').checked,
    cell_voltage_max_mv: parseInt(document.getElementById('config-cell-volt-max').value) || 3650,

    cell_volt_low_enabled: document.getElementById('config-cell-volt-low-enabled').checked,
    cell_voltage_min_mv: parseInt(document.getElementById('config-cell-volt-min').value) || 2800,

    monitor_tinybms_events: document.getElementById('config-monitor-events').checked,
    monitor_status_changes: document.getElementById('config-monitor-status').checked
  };

  try {
    const response = await fetch('/api/alerts/config', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(config)
    });

    if (response.ok) {
      alert('Configuration enregistrée avec succès');
    } else {
      alert('Erreur lors de l\'enregistrement de la configuration');
    }
  } catch (error) {
    console.error('Failed to save alert configuration:', error);
    alert('Erreur lors de l\'enregistrement de la configuration');
  }
}

// Helper functions
function getSeverityClass(severity) {
  switch (severity) {
    case 2: return 'danger';  // Critical
    case 1: return 'warning'; // Warning
    case 0: return 'info';    // Info
    default: return 'secondary';
  }
}

function getSeverityLabel(severity) {
  switch (severity) {
    case 2: return 'CRITIQUE';
    case 1: return 'AVERTISSEMENT';
    case 0: return 'INFO';
    default: return 'INCONNU';
  }
}

function getAlertTitle(type) {
  const titles = {
    1: 'Température Élevée',
    2: 'Température Basse',
    3: 'Tension Cellule Haute',
    4: 'Tension Cellule Basse',
    5: 'Tension Pack Haute',
    6: 'Tension Pack Basse',
    7: 'Courant de Décharge Élevé',
    8: 'Courant de Charge Élevé',
    9: 'SOC Faible',
    10: 'SOH Faible',
    11: 'Déséquilibre Cellules Élevé',
    20: 'En Charge',
    21: 'Chargement Complet',
    22: 'En Décharge',
    23: 'En Régénération',
    24: 'Au Repos',
    25: 'Défaut Détecté',
  };
  return titles[type] || `Alerte Type ${type}`;
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
  // Connect to WebSocket
  connectAlertsWebSocket();

  // Setup form handler
  const form = document.getElementById('alert-config-form');
  if (form) {
    form.addEventListener('submit', saveAlertConfig);
  }

  // Refresh every 10 seconds
  setInterval(() => {
    updateAlertBadge();
  }, 10000);

  // Load configuration when alerts tab is shown
  const alertsTab = document.querySelector('[data-tab="alerts"]');
  if (alertsTab) {
    alertsTab.addEventListener('click', () => {
      setTimeout(() => {
        refreshActiveAlerts();
        refreshAlertHistory();
        refreshAlertStatistics();
        loadAlertConfig();
      }, 100);
    });
  }
});
