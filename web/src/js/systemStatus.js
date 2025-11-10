/**
 * System Status Manager
 * Manages LED status indicators for system modules
 */

const LED_STATUS = {
  UNKNOWN: 'unknown',
  OK: 'ok',
  ERROR: 'error',
  WARNING: 'warning',
  CONNECTING: 'connecting',
};

const MODULES = {
  WIFI: 'wifi',
  STORAGE: 'storage',
  UART: 'uart',
  CAN: 'can',
  MQTT: 'mqtt',
  WEBSERVER: 'webserver',
};

export class SystemStatus {
  constructor() {
    this.modules = new Map();
    this.lastUpdate = null;
    this.initialized = false;

    // Initialize module status tracking
    Object.values(MODULES).forEach(module => {
      this.modules.set(module, LED_STATUS.UNKNOWN);
    });

    // WebServer is always OK if we can load the page
    this.setModuleStatus(MODULES.WEBSERVER, LED_STATUS.OK);
  }

  /**
   * Initialize the system status display
   */
  init() {
    if (this.initialized) {
      return;
    }

    this.updateDisplay();
    this.initialized = true;
  }

  /**
   * Update module status
   * @param {string} module - Module name (from MODULES constant)
   * @param {string} status - Status (from LED_STATUS constant)
   */
  setModuleStatus(module, status) {
    if (!Object.values(MODULES).includes(module)) {
      console.warn(`Unknown module: ${module}`);
      return;
    }

    if (!Object.values(LED_STATUS).includes(status)) {
      console.warn(`Unknown status: ${status}`);
      return;
    }

    this.modules.set(module, status);
    this.lastUpdate = Date.now();
    this.updateDisplay();
  }

  /**
   * Update LED display for a specific module
   * @param {string} module - Module name
   */
  updateModuleDisplay(module) {
    const status = this.modules.get(module);
    const moduleElements = document.querySelectorAll(`.status-module[data-module="${module}"]`);

    moduleElements.forEach((moduleElement) => {
      const ledElement = moduleElement.querySelector('.status-led');
      if (!ledElement) {
        return;
      }

      Object.values(LED_STATUS).forEach(s => {
        ledElement.classList.remove(`status-led-${s}`);
      });

      ledElement.classList.add(`status-led-${status}`);
    });
  }

  /**
   * Update all module displays
   */
  updateDisplay() {
    Object.values(MODULES).forEach(module => {
      this.updateModuleDisplay(module);
    });
  }

  /**
   * Handle telemetry data update
   * @param {Object} data - Telemetry data from WebSocket
   */
  handleTelemetryUpdate(data) {
    // UART BMS: Check if we have valid data
    if (data && data.pack_voltage_v !== undefined && data.pack_voltage_v > 0) {
      this.setModuleStatus(MODULES.UART, LED_STATUS.OK);
    } else {
      this.setModuleStatus(MODULES.UART, LED_STATUS.WARNING);
    }

    // CAN Bus: Check if we have CAN-related data
    if (data && (data.energy_charged_wh !== undefined || data.energy_discharged_wh !== undefined)) {
      this.setModuleStatus(MODULES.CAN, LED_STATUS.OK);
    }
  }

  /**
   * Handle WebSocket events
   * @param {Object} event - Event data from WebSocket
   */
  handleEvent(event) {
    if (!event) {
      return;
    }

    const eventKey = typeof event.key === 'string' ? event.key : null;
    const eventType = typeof event.type === 'string' ? event.type : null;
    let eventId = null;

    if (typeof event.event_id === 'number') {
      eventId = event.event_id;
    } else if (typeof event.event_id === 'string') {
      const value = event.event_id.trim();
      if (value.startsWith('0x')) {
        eventId = Number.parseInt(value, 16);
      } else {
        const parsed = Number(value);
        eventId = Number.isNaN(parsed) ? null : parsed;
      }
    }

    if (eventKey || eventType === 'wifi' || eventType === 'storage') {
      switch (eventKey) {
        case 'wifi_sta_start':
        case 'wifi_sta_connected':
          this.setModuleStatus(MODULES.WIFI, LED_STATUS.CONNECTING);
          return;
        case 'wifi_sta_got_ip':
          this.setModuleStatus(MODULES.WIFI, LED_STATUS.OK);
          return;
        case 'wifi_sta_disconnected':
        case 'wifi_sta_lost_ip':
        case 'wifi_ap_started':
          this.setModuleStatus(MODULES.WIFI, LED_STATUS.WARNING);
          return;
        case 'wifi_ap_stopped':
        case 'wifi_ap_client_connected':
        case 'wifi_ap_client_disconnected':
          this.setModuleStatus(MODULES.WIFI, LED_STATUS.CONNECTING);
          return;
        case 'storage_history_ready':
          this.setModuleStatus(MODULES.STORAGE, LED_STATUS.OK);
          return;
        case 'storage_history_unavailable':
          this.setModuleStatus(MODULES.STORAGE, LED_STATUS.ERROR);
          return;
        default:
          break;
      }
    }

    if (eventId === null) {
      return;
    }

    if (eventId === 0x1300) {
      this.setModuleStatus(MODULES.WIFI, LED_STATUS.CONNECTING);
    } else if (eventId === 0x1303) {
      this.setModuleStatus(MODULES.WIFI, LED_STATUS.OK);
    } else if (eventId === 0x1302 || eventId === 0x1304) {
      this.setModuleStatus(MODULES.WIFI, LED_STATUS.WARNING);
    } else if (eventId === 0x1310) {
      this.setModuleStatus(MODULES.WIFI, LED_STATUS.WARNING);
    } else if (eventId === 0x1400) {
      this.setModuleStatus(MODULES.STORAGE, LED_STATUS.OK);
    } else if (eventId === 0x1401) {
      this.setModuleStatus(MODULES.STORAGE, LED_STATUS.ERROR);
    } else if (eventId === 0x1100 || eventId === 0x1101 || eventId === 0x1102) {
      this.setModuleStatus(MODULES.UART, LED_STATUS.OK);
    } else if (eventId === 0x1200 || eventId === 0x1201 || eventId === 0x1202) {
      this.setModuleStatus(MODULES.CAN, LED_STATUS.OK);
    }
  }

  /**
   * Handle MQTT status update
   * @param {Object} status - MQTT status data from API
   */
  handleMqttStatus(status) {
    if (!status) {
      return;
    }

    if (status.connected) {
      this.setModuleStatus(MODULES.MQTT, LED_STATUS.OK);
    } else if (status.client_started && !status.wifi_connected) {
      this.setModuleStatus(MODULES.MQTT, LED_STATUS.WARNING);
    } else if (status.client_started) {
      this.setModuleStatus(MODULES.MQTT, LED_STATUS.CONNECTING);
    } else {
      this.setModuleStatus(MODULES.MQTT, LED_STATUS.UNKNOWN);
    }
  }

  /**
   * Set initial status from API data
   * @param {Object} initialData - Initial status data
   */
  setInitialStatus(initialData) {
    // Assume WiFi is OK if we got the data
    this.setModuleStatus(MODULES.WIFI, LED_STATUS.OK);

    // Check if we have battery data (UART is working)
    if (initialData && initialData.pack_voltage_v !== undefined && initialData.pack_voltage_v > 0) {
      this.setModuleStatus(MODULES.UART, LED_STATUS.OK);
    }

    // Storage status will be set by events
    // CAN status will be set by events or telemetry
    // MQTT status will be fetched separately
  }

  /**
   * Get current status for all modules
   * @returns {Object} Map of module statuses
   */
  getStatus() {
    const status = {};
    this.modules.forEach((value, key) => {
      status[key] = value;
    });
    return status;
  }
}
