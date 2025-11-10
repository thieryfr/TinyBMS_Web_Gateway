/**
 * TinyBMS-GW Enhanced Test Server
 * Serveur de test amélioré avec simulation réaliste de tous les modules
 * Version: 2.0.0
 */

import express from 'express';
import { createServer } from 'http';
import { WebSocketServer } from 'ws';
import cors from 'cors';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

// Import des générateurs de données améliorés
import { TelemetrySimulator } from './simulators/telemetry-simulator.js';
import { ConfigManager } from './simulators/config-manager.js';
import { HistoryManager } from './simulators/history-manager.js';
import { RegistersManager } from './simulators/registers-manager.js';
import { UartSimulator } from './simulators/uart-simulator.js';
import { CanSimulator } from './simulators/can-simulator.js';
import { EventSimulator } from './simulators/event-simulator.js';
import { AlarmSimulator } from './simulators/alarm-simulator.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Configuration
const PORT = process.env.PORT || 3000;
const WEB_DIR = process.env.WEB_DIR || path.join(__dirname, '..', 'web');

// Création du serveur Express
const app = express();
const server = createServer(app);

// Initialisation des simulateurs
const telemetrySimulator = new TelemetrySimulator();
const configManager = new ConfigManager();
const historyManager = new HistoryManager();
const registersManager = new RegistersManager();
const uartSimulator = new UartSimulator();
const canSimulator = new CanSimulator();
const eventSimulator = new EventSimulator();
const alarmSimulator = new AlarmSimulator(configManager);

let lastTelemetrySnapshot = telemetrySimulator.getCurrentData();

// Configuration des WebSocket servers
const wsTelemetry = new WebSocketServer({ noServer: true });
const wsEvents = new WebSocketServer({ noServer: true });
const wsUart = new WebSocketServer({ noServer: true });
const wsCan = new WebSocketServer({ noServer: true });

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(WEB_DIR));

// Logging middleware amélioré
app.use((req, res, next) => {
  const timestamp = new Date().toISOString();
  const method = req.method;
  const url = req.originalUrl;
  const clientIp = req.ip || req.connection.remoteAddress;
  
  console.log(`[${timestamp}] ${method} ${url} - IP: ${clientIp}`);
  
  // Log du body pour les POST/PUT
  if ((method === 'POST' || method === 'PUT') && Object.keys(req.body).length > 0) {
    console.log('  Body:', JSON.stringify(req.body).substring(0, 200));
  }
  
  next();
});

// ============================================================================
// REST API Endpoints
// ============================================================================

/**
 * GET /api/status
 * Status complet du système avec toutes les métriques
 */
app.get('/api/status', (req, res) => {
  const telemetry = telemetrySimulator.getCurrentData();
  const config = configManager.getConfig();
  const alarms = alarmSimulator.getActiveAlarms();
  
  res.json({
    device: {
      name: config.device.name,
      hostname: config.device.hostname,
      uptime_seconds: Math.floor(process.uptime()),
      free_heap_bytes: process.memoryUsage().heapUsed,
      total_heap_bytes: process.memoryUsage().heapTotal,
      firmware_version: "2.0.0-test",
      hardware_version: "ESP32-S3",
      boot_count: 1,
      reset_reason: "power_on"
    },
    battery: {
      ...telemetry,
      alarms: alarms.battery || [],
      warnings: alarms.warnings || []
    },
    connectivity: {
      wifi: {
        connected: true,
        ssid: config.wifi.ssid || "TestNetwork",
        rssi_dbm: -45 + Math.random() * 20,
        ip_address: "192.168.1.100",
        mac_address: "AA:BB:CC:DD:EE:FF"
      },
      mqtt: {
        connected: config.mqtt.enabled,
        broker_uri: config.mqtt.broker_uri,
        client_id: config.mqtt.client_id,
        messages_sent: Math.floor(Math.random() * 10000),
        messages_received: Math.floor(Math.random() * 5000),
        last_error: null
      },
      can: {
        enabled: config.can.enabled,
        bitrate: config.can.bitrate,
        frames_sent: canSimulator.getStats().framesSent,
        frames_received: canSimulator.getStats().framesReceived,
        errors: canSimulator.getStats().errors
      },
      uart: {
        enabled: config.uart.enabled,
        baudrate: config.uart.baudrate,
        frames_sent: uartSimulator.getStats().framesSent,
        frames_received: uartSimulator.getStats().framesReceived,
        checksum_errors: uartSimulator.getStats().checksumErrors
      }
    },
    modules: {
      telemetry: {
        active: true,
        update_rate_hz: 1,
        last_update_ms: Date.now()
      },
      history: {
        active: true,
        entries_count: historyManager.getEntryCount(),
        max_entries: 512,
        storage_used_bytes: historyManager.getStorageUsed()
      },
      registers: {
        active: true,
        total_registers: registersManager.getRegisterCount(),
        writable_registers: registersManager.getWritableCount()
      }
    }
  });
});

/**
 * GET /api/config
 * Configuration complète du dispositif
 */
app.get('/api/config', (req, res) => {
  const config = configManager.getConfig();
  res.json(config);
});

/**
 * POST /api/config
 * Mise à jour de la configuration
 */
app.post('/api/config', (req, res) => {
  try {
    const updated = configManager.updateConfig(req.body);
    
    // Notifier les clients WebSocket
    broadcastToClients(wsEvents, {
      type: 'config_updated',
      data: updated,
      timestamp: Date.now()
    });
    
    // Enregistrer un événement
    eventSimulator.addEvent('CONFIG_CHANGE', 'Configuration updated', 'info');
    
    res.json({ 
      success: true, 
      message: 'Configuration updated successfully',
      config: updated 
    });
  } catch (error) {
    console.error('Config update error:', error);
    res.status(400).json({ 
      success: false, 
      error: error.message 
    });
  }
});

/**
 * GET /api/mqtt/config
 */
app.get('/api/mqtt/config', (req, res) => {
  const mqttConfig = configManager.getMqttConfig();
  res.json(mqttConfig);
});

/**
 * POST /api/mqtt/config
 */
app.post('/api/mqtt/config', (req, res) => {
  try {
    const updated = configManager.updateMqttConfig(req.body);
    
    broadcastToClients(wsEvents, {
      type: 'mqtt_config_updated',
      data: updated,
      timestamp: Date.now()
    });
    
    res.json({ 
      success: true, 
      config: updated 
    });
  } catch (error) {
    res.status(400).json({ 
      success: false, 
      error: error.message 
    });
  }
});

/**
 * GET /api/mqtt/status
 */
app.get('/api/mqtt/status', (req, res) => {
  const config = configManager.getMqttConfig();
  res.json({
    connected: config.enabled && Math.random() > 0.1, // 90% du temps connecté
    broker_uri: config.broker_uri,
    client_id: config.client_id,
    uptime_seconds: Math.floor(Math.random() * 3600),
    messages_published: Math.floor(Math.random() * 10000),
    messages_received: Math.floor(Math.random() * 5000),
    last_error: null,
    topics_subscribed: [
      `${config.topic_prefix}/command`,
      `${config.topic_prefix}/config`
    ]
  });
});

/**
 * GET /api/history
 */
app.get('/api/history', (req, res) => {
  const limit = parseInt(req.query.limit) || 512;
  const offset = parseInt(req.query.offset) || 0;
  const history = historyManager.getHistory(limit, offset);
  res.json(history);
});

/**
 * GET /api/history/files
 */
app.get('/api/history/files', (req, res) => {
  const files = historyManager.getArchiveFiles();
  res.json(files);
});

/**
 * GET /api/history/download
 */
app.get('/api/history/download', (req, res) => {
  const filename = req.query.file || `history_${new Date().toISOString().split('T')[0]}.csv`;
  const csv = historyManager.generateCSV();
  
  res.setHeader('Content-Type', 'text/csv');
  res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
  res.send(csv);
});

/**
 * DELETE /api/history
 * Effacer l'historique
 */
app.delete('/api/history', (req, res) => {
  historyManager.clearHistory();
  eventSimulator.addEvent('HISTORY_CLEARED', 'History data cleared', 'warning');
  res.json({ 
    success: true, 
    message: 'History cleared successfully' 
  });
});

/**
 * GET /api/registers
 */
app.get('/api/registers', (req, res) => {
  const category = req.query.category;
  const registers = registersManager.getRegisters(category);
  res.json(registers);
});

/**
 * POST /api/registers
 */
app.post('/api/registers', (req, res) => {
  try {
    const { registers } = req.body;
    const result = registersManager.updateRegisters(registers);
    
    if (result.success) {
      broadcastToClients(wsEvents, {
        type: 'registers_updated',
        data: result.updated,
        timestamp: Date.now()
      });
      
      eventSimulator.addEvent('REGISTERS_UPDATE', `${result.updated.length} registers updated`, 'info');
    }
    
    res.json(result);
  } catch (error) {
    res.status(400).json({ 
      success: false, 
      error: error.message 
    });
  }
});

/**
 * GET /api/registers/export
 * Exporter les registres en JSON
 */
app.get('/api/registers/export', (req, res) => {
  const registers = registersManager.exportRegisters();
  res.json(registers);
});

/**
 * POST /api/registers/import
 * Importer des registres depuis JSON
 */
app.post('/api/registers/import', (req, res) => {
  try {
    const result = registersManager.importRegisters(req.body);
    res.json({ 
      success: true, 
      message: `${result.imported} registers imported successfully` 
    });
  } catch (error) {
    res.status(400).json({ 
      success: false, 
      error: error.message 
    });
  }
});

/**
 * GET /api/uart/status
 */
app.get('/api/uart/status', (req, res) => {
  const stats = uartSimulator.getStats();
  const config = configManager.getConfig();
  res.json({
    enabled: config.uart.enabled,
    baudrate: config.uart.baudrate,
    protocol: config.uart.protocol,
    ...stats,
    last_frame: uartSimulator.getLastFrame()
  });
});

/**
 * GET /api/can/status
 */
app.get('/api/can/status', (req, res) => {
  const stats = canSimulator.getStats();
  const config = configManager.getConfig();
  res.json({
    enabled: config.can.enabled,
    bitrate: config.can.bitrate,
    protocol: config.can.protocol,
    ...stats,
    last_frame: canSimulator.getLastFrame()
  });
});

/**
 * GET /api/events
 * Obtenir les derniers événements système
 */
app.get('/api/events', (req, res) => {
  const limit = parseInt(req.query.limit) || 100;
  const events = eventSimulator.getEvents(limit);
  res.json(events);
});

/**
 * GET /api/alarms
 * Obtenir les alarmes actives
 */
app.get('/api/alarms', (req, res) => {
  const alarms = alarmSimulator.getAllAlarms();
  res.json(alarms);
});

/**
 * POST /api/alarms/acknowledge
 * Acquitter une alarme
 */
app.post('/api/alarms/acknowledge', (req, res) => {
  const { alarm_id } = req.body;
  const result = alarmSimulator.acknowledgeAlarm(alarm_id);
  
  if (result.success) {
    broadcastToClients(wsEvents, {
      type: 'alarm_acknowledged',
      data: { alarm_id },
      timestamp: Date.now()
    });
  }
  
  res.json(result);
});

/**
 * POST /api/command
 * Envoyer une commande au BMS
 */
app.post('/api/command', (req, res) => {
  const { command, parameters } = req.body;
  
  // Simuler l'exécution de commande
  setTimeout(() => {
    eventSimulator.addEvent('COMMAND_EXECUTED', `Command: ${command}`, 'info');
  }, 100);
  
  res.json({
    success: true,
    command,
    response: `Command ${command} executed successfully`,
    execution_time_ms: Math.floor(Math.random() * 100) + 50
  });
});

/**
 * GET /api/diagnostics
 * Diagnostics système complets
 */
app.get('/api/diagnostics', (req, res) => {
  const telemetry = telemetrySimulator.getCurrentData();
  const config = configManager.getConfig();
  
  res.json({
    system: {
      uptime_seconds: Math.floor(process.uptime()),
      memory: process.memoryUsage(),
      cpu_load: process.cpuUsage(),
      temperature_c: 35 + Math.random() * 10
    },
    battery: {
      health_check: telemetrySimulator.performHealthCheck(),
      balancing_active: telemetry.cells_balancing_active.some(b => b),
      charge_cycles: Math.floor(Math.random() * 500),
      capacity_fade_percent: Math.random() * 5
    },
    communications: {
      mqtt: {
        latency_ms: Math.floor(Math.random() * 100),
        packet_loss_percent: Math.random() * 2
      },
      uart: {
        error_rate: uartSimulator.getStats().errorRate || 0
      },
      can: {
        bus_load_percent: Math.random() * 50
      }
    },
    self_test: {
      adc: "PASS",
      flash: "PASS",
      ram: "PASS",
      watchdog: "PASS",
      sensors: "PASS"
    }
  });
});

// ============================================================================
// WebSocket Handlers
// ============================================================================

/**
 * Gestion des connexions WebSocket pour la télémétrie
 */
wsTelemetry.on('connection', (ws, req) => {
  const clientId = generateClientId();
  console.log(`[WS] Telemetry client connected: ${clientId}`);
  
  ws.on('close', () => {
    console.log(`[WS] Telemetry client disconnected: ${clientId}`);
  });
  
  ws.on('error', (error) => {
    console.error(`[WS] Telemetry error for ${clientId}:`, error);
  });
});

/**
 * Gestion des connexions WebSocket pour les événements
 */
wsEvents.on('connection', (ws, req) => {
  const clientId = generateClientId();
  console.log(`[WS] Events client connected: ${clientId}`);
  
  // Envoyer les derniers événements à la connexion
  const recentEvents = eventSimulator.getEvents(10);
  ws.send(JSON.stringify({
    type: 'initial_events',
    data: recentEvents
  }));
  
  ws.on('close', () => {
    console.log(`[WS] Events client disconnected: ${clientId}`);
  });
});

/**
 * Gestion des connexions WebSocket pour UART
 */
wsUart.on('connection', (ws, req) => {
  const clientId = generateClientId();
  console.log(`[WS] UART client connected: ${clientId}`);
  
  ws.on('close', () => {
    console.log(`[WS] UART client disconnected: ${clientId}`);
  });
});

/**
 * Gestion des connexions WebSocket pour CAN
 */
wsCan.on('connection', (ws, req) => {
  const clientId = generateClientId();
  console.log(`[WS] CAN client connected: ${clientId}`);
  
  ws.on('close', () => {
    console.log(`[WS] CAN client disconnected: ${clientId}`);
  });
});

// Upgrade du serveur HTTP pour gérer les WebSockets
server.on('upgrade', (request, socket, head) => {
  const pathname = request.url;
  
  if (pathname === '/ws/telemetry') {
    wsTelemetry.handleUpgrade(request, socket, head, (ws) => {
      wsTelemetry.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/events') {
    wsEvents.handleUpgrade(request, socket, head, (ws) => {
      wsEvents.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/uart') {
    wsUart.handleUpgrade(request, socket, head, (ws) => {
      wsUart.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/can') {
    wsCan.handleUpgrade(request, socket, head, (ws) => {
      wsCan.emit('connection', ws, request);
    });
  } else {
    socket.destroy();
  }
});

// ============================================================================
// Broadcast Functions
// ============================================================================

function broadcastToClients(wsServer, message) {
  const data = JSON.stringify(message);
  wsServer.clients.forEach((client) => {
    if (client.readyState === 1) { // WebSocket.OPEN
      client.send(data);
    }
  });
}

function generateClientId() {
  return Math.random().toString(36).substring(2, 9);
}

// ============================================================================
// Intervalles de diffusion
// ============================================================================

/**
 * Diffusion des données de télémétrie à 1Hz
 */
setInterval(() => {
  const telemetryData = telemetrySimulator.update();
  lastTelemetrySnapshot = telemetryData;

  broadcastToClients(wsTelemetry, {
    type: 'telemetry',
    data: telemetryData,
    timestamp: Date.now()
  });

  // Vérifier les alarmes
  const alarms = alarmSimulator.checkAlarms(telemetryData);
  if (alarms.new.length > 0) {
    broadcastToClients(wsEvents, {
      type: 'new_alarms',
      data: alarms.new,
      timestamp: Date.now()
    });
  }
}, 1000);

/**
 * Ajout d'échantillon d'historique toutes les 60 secondes
 */
setInterval(() => {
  historyManager.addEntry(lastTelemetrySnapshot);

  console.log(`[History] Added sample #${historyManager.getEntryCount()}`);
}, 60000);

/**
 * Diffusion des trames UART à 2Hz
 */
setInterval(() => {
  const frame = uartSimulator.generateFrame(lastTelemetrySnapshot);

  broadcastToClients(wsUart, {
    type: 'uart_frame',
    data: frame,
    timestamp: Date.now()
  });
}, 500);

/**
 * Diffusion des trames CAN à 10Hz
 */
setInterval(() => {
  const frame = canSimulator.generateFrame(lastTelemetrySnapshot);

  broadcastToClients(wsCan, {
    type: 'can_frame',
    data: frame,
    timestamp: Date.now()
  });
}, 100);

/**
 * Génération d'événements aléatoires
 */
setInterval(() => {
  if (Math.random() < 0.1) { // 10% de chance par intervalle
    const event = eventSimulator.generateRandomEvent({
      phase: lastTelemetrySnapshot.bms_status,
      soc: lastTelemetrySnapshot.state_of_charge_pct,
      pack_voltage_v: lastTelemetrySnapshot.pack_voltage_v,
    });

    broadcastToClients(wsEvents, {
      type: 'system_event',
      data: event,
      timestamp: Date.now()
    });
  }
}, 5000);

// ============================================================================
// Démarrage du serveur
// ============================================================================

server.listen(PORT, () => {
  console.log('\n' + '='.repeat(70));
  console.log('  TinyBMS-GW Enhanced Test Server v2.0');
  console.log('='.repeat(70));
  console.log('');
  console.log(`  🌐 Web Interface:  http://localhost:${PORT}`);
  console.log(`  📁 Web Directory:  ${WEB_DIR}`);
  console.log('');
  console.log('  📡 WebSocket Endpoints:');
  console.log(`     • /ws/telemetry     - Télémétrie batterie temps réel (1Hz)`);
  console.log(`     • /ws/events        - Événements système`);
  console.log(`     • /ws/uart          - Trames UART (2Hz)`);
  console.log(`     • /ws/can           - Trames CAN (10Hz)`);
  console.log('');
  console.log('  🔌 REST API Endpoints:');
  console.log('     • GET  /api/status             - Status système complet');
  console.log('     • GET  /api/config             - Configuration dispositif');
  console.log('     • POST /api/config             - Mise à jour config');
  console.log('     • GET  /api/mqtt/config        - Config MQTT');
  console.log('     • POST /api/mqtt/config        - Mise à jour MQTT');
  console.log('     • GET  /api/mqtt/status        - Status MQTT');
  console.log('     • GET  /api/uart/status        - Status UART');
  console.log('     • GET  /api/can/status         - Status CAN');
  console.log('     • GET  /api/history            - Données historique');
  console.log('     • GET  /api/history/files      - Fichiers archive');
  console.log('     • GET  /api/history/download   - Télécharger CSV');
  console.log('     • DELETE /api/history          - Effacer historique');
  console.log('     • GET  /api/registers          - Registres BMS');
  console.log('     • POST /api/registers          - Mise à jour registres');
  console.log('     • GET  /api/events             - Événements système');
  console.log('     • GET  /api/alarms             - Alarmes actives');
  console.log('     • POST /api/alarms/acknowledge - Acquitter alarme');
  console.log('     • POST /api/command            - Commande BMS');
  console.log('     • GET  /api/diagnostics        - Diagnostics complets');
  console.log('');
  console.log('  📊 Modules de simulation actifs:');
  console.log('     ✓ Télémétrie batterie (16S LiFePO4)');
  console.log('     ✓ Gestion configuration');
  console.log('     ✓ Historique avec archivage');
  console.log('     ✓ Registres BMS (lecture/écriture)');
  console.log('     ✓ Communication UART');
  console.log('     ✓ Communication CAN');
  console.log('     ✓ Événements système');
  console.log('     ✓ Gestion des alarmes');
  console.log('');
  console.log('  Appuyez sur Ctrl+C pour arrêter le serveur');
  console.log('='.repeat(70));
  console.log('');
});

// Arrêt propre du serveur
process.on('SIGINT', () => {
  console.log('\n\n⏹️  Arrêt du serveur en cours...');
  
  // Sauvegarder l'état si nécessaire
  configManager.saveState();
  historyManager.saveState();
  
  server.close(() => {
    console.log('✅ Serveur arrêté proprement');
    process.exit(0);
  });
  
  // Forcer l'arrêt après 5 secondes
  setTimeout(() => {
    console.error('⚠️  Arrêt forcé après timeout');
    process.exit(1);
  }, 5000);
});

// Gestion des erreurs non capturées
process.on('uncaughtException', (error) => {
  console.error('❌ Erreur non capturée:', error);
  eventSimulator.addEvent('SYSTEM_ERROR', error.message, 'error');
});

process.on('unhandledRejection', (reason, promise) => {
  console.error('❌ Promise rejetée non gérée:', reason);
  eventSimulator.addEvent('PROMISE_REJECTION', String(reason), 'error');
});

export default app;
