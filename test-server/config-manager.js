/**
 * Gestionnaire de configuration
 * Gère la configuration du système avec validation et persistance
 */

import fs from 'fs';
import path from 'path';

export class ConfigManager {
  constructor(configFile = null) {
    this.configFile = configFile || path.join(process.cwd(), 'config.json');
    this.config = this.loadConfig();
    this.defaultConfig = this.getDefaultConfig();
  }

  /**
   * Configuration par défaut
   */
  getDefaultConfig() {
    return {
      device: {
        name: 'TinyBMS-GW Test',
        hostname: 'tinybms-test',
        model: 'ESP32-S3',
        serial_number: 'TEST-' + Math.random().toString(36).substring(2, 9).toUpperCase(),
        firmware_version: '2.0.0',
        hardware_version: '1.0',
        manufacturer: 'TinyBMS'
      },
      
      wifi: {
        enabled: true,
        ssid: 'TestNetwork',
        password: '',
        ip_mode: 'dhcp', // 'dhcp' ou 'static'
        static_ip: '192.168.1.100',
        gateway: '192.168.1.1',
        subnet: '255.255.255.0',
        dns1: '8.8.8.8',
        dns2: '8.8.4.4',
        hostname: 'tinybms-gw',
        tx_power: 20 // dBm
      },
      
      mqtt: {
        enabled: true,
        broker_uri: 'mqtt://test.mosquitto.org:1883',
        client_id: 'tinybms_' + Math.random().toString(36).substring(2, 9),
        username: '',
        password: '',
        topic_prefix: 'tinybms/battery',
        publish_interval: 5, // seconds
        qos: 1,
        retain: true,
        clean_session: true,
        keep_alive: 60,
        tls_enabled: false,
        tls_verify: false
      },
      
      can: {
        enabled: true,
        bitrate: 500000, // 500 kbps
        protocol: 'CAN_2.0B', // or 'CAN_FD'
        mode: 'normal', // 'normal', 'listen_only', 'loopback'
        tx_queue_size: 10,
        rx_queue_size: 10,
        filters: [
          { id: 0x100, mask: 0x7FF, extended: false },
          { id: 0x200, mask: 0x7FF, extended: false }
        ],
        protocol_type: 'victron', // 'victron', 'pylontech', 'generic'
        node_id: 1
      },
      
      uart: {
        enabled: true,
        baudrate: 115200,
        data_bits: 8,
        stop_bits: 1,
        parity: 'none', // 'none', 'even', 'odd'
        flow_control: 'none', // 'none', 'hardware', 'software'
        protocol: 'tinybms', // 'tinybms', 'modbus', 'custom'
        timeout_ms: 1000,
        rx_buffer_size: 1024,
        tx_buffer_size: 1024
      },
      
      battery: {
        chemistry: 'LiFePO4', // 'LiFePO4', 'Li-ion', 'LTO'
        cells_series: 16,
        cells_parallel: 1,
        capacity_ah: 100,
        nominal_voltage_v: 51.2,
        
        // Protection settings
        overvoltage_protection_v: 3.65,
        undervoltage_protection_v: 2.5,
        overcurrent_charge_a: 50,
        overcurrent_discharge_a: 100,
        overtemperature_charge_c: 45,
        overtemperature_discharge_c: 55,
        undertemperature_charge_c: 0,
        undertemperature_discharge_c: -10,
        
        // Balancing settings
        balancing_enabled: true,
        balancing_start_voltage_v: 3.4,
        balancing_delta_mv: 20,
        balancing_current_ma: 50,
        
        // Charge settings
        charge_voltage_v: 58.4, // 3.65V * 16
        float_voltage_v: 54.4,  // 3.4V * 16
        charge_current_a: 20,
        charge_cutoff_current_a: 2,
        
        // SOC calibration
        soc_100_voltage_v: 3.65,
        soc_0_voltage_v: 2.8,
        soc_lookup_table: [
          { voltage: 2.8, soc: 0 },
          { voltage: 3.0, soc: 5 },
          { voltage: 3.1, soc: 10 },
          { voltage: 3.2, soc: 20 },
          { voltage: 3.25, soc: 50 },
          { voltage: 3.3, soc: 80 },
          { voltage: 3.4, soc: 95 },
          { voltage: 3.65, soc: 100 }
        ]
      },
      
      telemetry: {
        enabled: true,
        update_interval_ms: 1000,
        publish_interval_s: 5,
        averaging_samples: 10,
        filters: {
          voltage: true,
          current: true,
          temperature: true
        }
      },
      
      history: {
        enabled: true,
        interval_seconds: 60,
        max_entries: 512,
        auto_export: false,
        export_format: 'csv', // 'csv', 'json'
        storage_location: 'internal', // 'internal', 'sd_card'
        compression: false
      },
      
      alarms: {
        enabled: true,
        buzzer_enabled: false,
        led_enabled: true,
        auto_acknowledge: false,
        
        thresholds: {
          voltage_high_warning_v: 3.55,
          voltage_high_critical_v: 3.65,
          voltage_low_warning_v: 2.9,
          voltage_low_critical_v: 2.5,
          current_high_warning_a: 40,
          current_high_critical_a: 50,
          temperature_high_warning_c: 45,
          temperature_high_critical_c: 55,
          soc_low_warning_pct: 20,
          soc_low_critical_pct: 10
        }
      },
      
      web: {
        enabled: true,
        port: 80,
        auth_enabled: false,
        username: 'admin',
        password: 'admin',
        session_timeout_minutes: 30,
        max_connections: 10,
        cors_enabled: true,
        api_rate_limit: 100 // requests per minute
      },
      
      system: {
        timezone: 'UTC',
        ntp_enabled: true,
        ntp_server: 'pool.ntp.org',
        watchdog_enabled: true,
        watchdog_timeout_s: 30,
        auto_restart: true,
        debug_mode: false,
        log_level: 'info', // 'debug', 'info', 'warning', 'error'
        
        power_save: {
          enabled: false,
          wifi_sleep: false,
          cpu_frequency_mhz: 240,
          light_sleep: false,
          deep_sleep: false,
          wake_interval_s: 60
        }
      },
      
      modbus: {
        enabled: false,
        slave_id: 1,
        tcp_port: 502,
        rtu_enabled: false,
        holding_registers_start: 40000,
        input_registers_start: 30000,
        coils_start: 0,
        discrete_inputs_start: 10000,
        byte_order: 'big_endian', // 'big_endian', 'little_endian'
        word_order: 'big_endian'
      }
    };
  }

  /**
   * Charge la configuration depuis le fichier ou utilise la configuration par défaut
   */
  loadConfig() {
    try {
      if (fs.existsSync(this.configFile)) {
        const data = fs.readFileSync(this.configFile, 'utf8');
        const loaded = JSON.parse(data);
        console.log(`[ConfigManager] Configuration loaded from ${this.configFile}`);
        return this.mergeWithDefaults(loaded);
      }
    } catch (error) {
      console.error('[ConfigManager] Error loading config:', error);
    }
    
    console.log('[ConfigManager] Using default configuration');
    return this.getDefaultConfig();
  }

  /**
   * Fusionne la configuration chargée avec les valeurs par défaut
   */
  mergeWithDefaults(loaded) {
    const defaults = this.getDefaultConfig();
    return this.deepMerge(defaults, loaded);
  }

  /**
   * Fusion profonde de deux objets
   */
  deepMerge(target, source) {
    const result = { ...target };
    
    for (const key in source) {
      if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
        result[key] = this.deepMerge(result[key] || {}, source[key]);
      } else {
        result[key] = source[key];
      }
    }
    
    return result;
  }

  /**
   * Sauvegarde la configuration dans un fichier
   */
  saveConfig() {
    try {
      fs.writeFileSync(this.configFile, JSON.stringify(this.config, null, 2));
      console.log(`[ConfigManager] Configuration saved to ${this.configFile}`);
      return true;
    } catch (error) {
      console.error('[ConfigManager] Error saving config:', error);
      return false;
    }
  }

  /**
   * Obtient la configuration complète
   */
  getConfig() {
    return JSON.parse(JSON.stringify(this.config));
  }

  /**
   * Obtient une section de configuration
   */
  getSection(section) {
    return this.config[section] ? 
      JSON.parse(JSON.stringify(this.config[section])) : null;
  }

  /**
   * Met à jour la configuration
   */
  updateConfig(updates) {
    // Valider les mises à jour
    const validated = this.validateConfig(updates);
    
    // Fusionner avec la configuration existante
    this.config = this.deepMerge(this.config, validated);
    
    // Sauvegarder si configuré
    if (process.env.PERSIST_CONFIG === 'true') {
      this.saveConfig();
    }
    
    console.log('[ConfigManager] Configuration updated');
    return this.getConfig();
  }

  /**
   * Valide les paramètres de configuration
   */
  validateConfig(config) {
    const validated = {};
    
    for (const section in config) {
      if (!this.config[section]) {
        console.warn(`[ConfigManager] Unknown section: ${section}`);
        continue;
      }
      
      validated[section] = {};
      
      for (const key in config[section]) {
        const value = config[section][key];
        
        // Validation basique du type
        if (this.config[section][key] !== undefined) {
          const expectedType = typeof this.config[section][key];
          const actualType = typeof value;
          
          if (expectedType !== actualType && value !== null) {
            console.warn(`[ConfigManager] Type mismatch for ${section}.${key}: expected ${expectedType}, got ${actualType}`);
            continue;
          }
          
          // Validations spécifiques
          if (section === 'wifi' && key === 'tx_power') {
            if (value < 0 || value > 20) {
              console.warn(`[ConfigManager] Invalid TX power: ${value} dBm`);
              continue;
            }
          }
          
          if (section === 'battery' && key === 'cells_series') {
            if (value < 1 || value > 32) {
              console.warn(`[ConfigManager] Invalid cell count: ${value}`);
              continue;
            }
          }
          
          if (section === 'uart' && key === 'baudrate') {
            const validBaudrates = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
            if (!validBaudrates.includes(value)) {
              console.warn(`[ConfigManager] Invalid baudrate: ${value}`);
              continue;
            }
          }
          
          validated[section][key] = value;
        } else {
          console.warn(`[ConfigManager] Unknown parameter: ${section}.${key}`);
        }
      }
    }
    
    return validated;
  }

  /**
   * Configuration MQTT spécifique
   */
  getMqttConfig() {
    return this.getSection('mqtt');
  }

  updateMqttConfig(updates) {
    return this.updateConfig({ mqtt: updates }).mqtt;
  }

  /**
   * Configuration de la batterie
   */
  getBatteryConfig() {
    return this.getSection('battery');
  }

  updateBatteryConfig(updates) {
    return this.updateConfig({ battery: updates }).battery;
  }

  /**
   * Réinitialise une section de configuration
   */
  resetSection(section) {
    if (this.defaultConfig[section]) {
      this.config[section] = JSON.parse(JSON.stringify(this.defaultConfig[section]));
      console.log(`[ConfigManager] Section ${section} reset to defaults`);
      return true;
    }
    return false;
  }

  /**
   * Réinitialise toute la configuration
   */
  resetAll() {
    this.config = this.getDefaultConfig();
    console.log('[ConfigManager] Configuration reset to defaults');
    if (process.env.PERSIST_CONFIG === 'true') {
      this.saveConfig();
    }
    return this.config;
  }

  /**
   * Exporte la configuration
   */
  exportConfig() {
    return {
      version: '2.0.0',
      timestamp: new Date().toISOString(),
      config: this.config
    };
  }

  /**
   * Importe une configuration
   */
  importConfig(data) {
    try {
      const imported = typeof data === 'string' ? JSON.parse(data) : data;
      const config = imported.config || imported;
      
      // Valider et fusionner
      this.config = this.mergeWithDefaults(config);
      
      if (process.env.PERSIST_CONFIG === 'true') {
        this.saveConfig();
      }
      
      console.log('[ConfigManager] Configuration imported successfully');
      return { success: true, config: this.config };
    } catch (error) {
      console.error('[ConfigManager] Import failed:', error);
      return { success: false, error: error.message };
    }
  }

  /**
   * Sauvegarde l'état actuel (pour l'arrêt du serveur)
   */
  saveState() {
    if (process.env.PERSIST_CONFIG === 'true') {
      this.saveConfig();
    }
  }
}
