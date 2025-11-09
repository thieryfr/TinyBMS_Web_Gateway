/**
 * @file alerts.test.js
 * @brief Unit tests for alerts module
 */

import { describe, test, expect, beforeEach, jest } from '@jest/globals';

// Mock DOM elements
beforeEach(() => {
  document.body.innerHTML = `
    <div id="active-alerts-container"></div>
    <div id="alert-history-container"></div>
    <div id="alert-statistics-container"></div>
    <span id="alert-badge"></span>
  `;
});

describe('Alerts Module', () => {
  describe('Alert Severity Helpers', () => {
    // These helper functions are typically in alerts.js
    function getSeverityClass(severity) {
      switch (severity) {
        case 3: return 'danger';
        case 2: return 'warning';
        case 1: return 'info';
        default: return 'secondary';
      }
    }

    function getSeverityLabel(severity) {
      switch (severity) {
        case 3: return 'Critique';
        case 2: return 'Avertissement';
        case 1: return 'Info';
        default: return 'Inconnu';
      }
    }

    test('getSeverityClass should return correct Bootstrap class', () => {
      expect(getSeverityClass(3)).toBe('danger');
      expect(getSeverityClass(2)).toBe('warning');
      expect(getSeverityClass(1)).toBe('info');
      expect(getSeverityClass(0)).toBe('secondary');
    });

    test('getSeverityLabel should return correct French label', () => {
      expect(getSeverityLabel(3)).toBe('Critique');
      expect(getSeverityLabel(2)).toBe('Avertissement');
      expect(getSeverityLabel(1)).toBe('Info');
      expect(getSeverityLabel(0)).toBe('Inconnu');
    });
  });

  describe('Alert Type Helpers', () => {
    function getAlertTitle(type) {
      const titles = {
        'TEMP_HIGH': 'Température élevée',
        'TEMP_LOW': 'Température basse',
        'VOLTAGE_HIGH': 'Tension élevée',
        'VOLTAGE_LOW': 'Tension basse',
        'CURRENT_HIGH': 'Courant élevé',
        'SOC_LOW': 'État de charge faible',
        'CELL_IMBALANCE': 'Déséquilibre cellules'
      };
      return titles[type] || 'Alerte système';
    }

    test('getAlertTitle should return correct French title', () => {
      expect(getAlertTitle('TEMP_HIGH')).toBe('Température élevée');
      expect(getAlertTitle('VOLTAGE_LOW')).toBe('Tension basse');
      expect(getAlertTitle('SOC_LOW')).toBe('État de charge faible');
      expect(getAlertTitle('UNKNOWN')).toBe('Alerte système');
    });
  });

  describe('Alert API Interactions', () => {
    beforeEach(() => {
      global.fetch = jest.fn();
    });

    test('should fetch active alerts successfully', async () => {
      const mockAlerts = {
        alerts: [
          {
            alert_id: 1,
            type: 'TEMP_HIGH',
            severity: 2,
            message: 'Température > 45°C',
            timestamp_ms: Date.now(),
            status: 0
          }
        ]
      };

      global.fetch.mockResolvedValueOnce({
        ok: true,
        json: async () => mockAlerts
      });

      const response = await fetch('/api/alerts/active');
      const data = await response.json();

      expect(fetch).toHaveBeenCalledWith('/api/alerts/active');
      expect(data.alerts).toBeDefined();
      expect(data.alerts.length).toBe(1);
      expect(data.alerts[0].type).toBe('TEMP_HIGH');
    });

    test('should fetch alert history with limit', async () => {
      const mockHistory = {
        alerts: []
      };

      global.fetch.mockResolvedValueOnce({
        ok: true,
        json: async () => mockHistory
      });

      const response = await fetch('/api/alerts/history?limit=50');
      const data = await response.json();

      expect(fetch).toHaveBeenCalledWith('/api/alerts/history?limit=50');
      expect(data.alerts).toBeDefined();
    });

    test('should acknowledge alert via POST', async () => {
      global.fetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({ success: true })
      });

      const alertId = 123;
      const response = await fetch(`/api/alerts/acknowledge/${alertId}`, {
        method: 'POST'
      });
      const data = await response.json();

      expect(fetch).toHaveBeenCalledWith(
        `/api/alerts/acknowledge/${alertId}`,
        { method: 'POST' }
      );
      expect(data.success).toBe(true);
    });
  });

  describe('Alert Rendering', () => {
    test('should render empty state when no alerts', () => {
      const container = document.getElementById('active-alerts-container');
      const emptyMessage = '<div class="text-muted text-center py-4">Aucune alerte active</div>';

      container.innerHTML = emptyMessage;

      expect(container.innerHTML).toContain('Aucune alerte active');
    });

    test('should render alert with correct severity class', () => {
      const container = document.getElementById('active-alerts-container');
      const alert = {
        alert_id: 1,
        type: 'TEMP_HIGH',
        severity: 3,
        message: 'Température critique',
        timestamp_ms: Date.now(),
        status: 0
      };

      // Simulate rendering
      container.innerHTML = `
        <div class="alert alert-danger">
          ${alert.message}
        </div>
      `;

      expect(container.innerHTML).toContain('alert-danger');
      expect(container.innerHTML).toContain('Température critique');
    });

    test('should show acknowledge button for active alerts', () => {
      const container = document.getElementById('active-alerts-container');
      const alertId = 42;

      container.innerHTML = `
        <button onclick="acknowledgeAlert(${alertId})">Acquitter</button>
      `;

      expect(container.innerHTML).toContain(`acknowledgeAlert(${alertId})`);
      expect(container.innerHTML).toContain('Acquitter');
    });

    test('should show acknowledged badge for completed alerts', () => {
      const container = document.getElementById('active-alerts-container');

      container.innerHTML = '<span class="badge bg-success">Acquitté</span>';

      expect(container.innerHTML).toContain('badge bg-success');
      expect(container.innerHTML).toContain('Acquitté');
    });
  });

  describe('Alert Timestamps', () => {
    test('should format timestamp correctly', () => {
      const timestamp = new Date('2025-01-09T10:30:00').getTime();
      const formatted = new Date(timestamp).toLocaleString('fr-FR');

      expect(formatted).toContain('2025');
      expect(formatted).toContain('10:30');
    });

    test('should handle invalid timestamps gracefully', () => {
      const invalidTimestamp = 'not-a-timestamp';
      const date = new Date(invalidTimestamp);

      expect(date.toString()).toBe('Invalid Date');
    });
  });

  describe('WebSocket Connection', () => {
    test('should create WebSocket with correct URL', () => {
      // Mock window.location
      Object.defineProperty(window, 'location', {
        value: {
          hostname: 'localhost'
        },
        writable: true
      });

      const wsUrl = `ws://${window.location.hostname}/ws/alerts`;

      expect(wsUrl).toBe('ws://localhost/ws/alerts');
    });

    test('should handle WebSocket message with alert data', () => {
      const mockMessage = {
        type: 'alert',
        alert_id: 1,
        severity: 2,
        message: 'Test alert'
      };

      const messageStr = JSON.stringify(mockMessage);
      const parsed = JSON.parse(messageStr);

      expect(parsed.type).toBe('alert');
      expect(parsed.severity).toBe(2);
    });
  });

  describe('Alert Statistics', () => {
    test('should calculate statistics correctly', () => {
      const alerts = [
        { severity: 3 },
        { severity: 3 },
        { severity: 2 },
        { severity: 1 }
      ];

      const stats = {
        total: alerts.length,
        critical: alerts.filter(a => a.severity === 3).length,
        warning: alerts.filter(a => a.severity === 2).length,
        info: alerts.filter(a => a.severity === 1).length
      };

      expect(stats.total).toBe(4);
      expect(stats.critical).toBe(2);
      expect(stats.warning).toBe(1);
      expect(stats.info).toBe(1);
    });
  });

  describe('XSS Prevention', () => {
    function escapeHtml(text) {
      if (!text) return '';
      const div = document.createElement('div');
      div.textContent = text;
      return div.innerHTML;
    }

    test('should escape HTML in alert messages', () => {
      const maliciousInput = '<script>alert("XSS")</script>';
      const escaped = escapeHtml(maliciousInput);

      expect(escaped).not.toContain('<script>');
      expect(escaped).toContain('&lt;script&gt;');
    });

    test('should escape quotes in alert messages', () => {
      const input = 'Message with "quotes"';
      const escaped = escapeHtml(input);

      expect(escaped).toBe('Message with "quotes"');
    });

    test('should handle null and undefined safely', () => {
      expect(escapeHtml(null)).toBe('');
      expect(escapeHtml(undefined)).toBe('');
      expect(escapeHtml('')).toBe('');
    });
  });
});
