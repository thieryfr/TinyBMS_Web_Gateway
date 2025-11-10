/**
 * TinyBMS-GW Local Test Server
 * Mock server for testing the web interface without ESP32 hardware
 */

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');
const crypto = require('crypto');

// Import mock data generators
const telemetry = require('./mock-data/telemetry');
const config = require('./mock-data/config');
const history = require('./mock-data/history');
const registers = require('./mock-data/registers');
const uart = require('./mock-data/uart');
const metrics = require('./mock-data/metrics');
const can = require('./mock-data/can');

// Configuration
const PORT = 3000;
const WEB_DIR = path.join(__dirname, '..', 'web');

// Create Express app
const app = express();
const server = http.createServer(app);

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(WEB_DIR));

// Logging middleware
app.use((req, res, next) => {
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path}`);
  next();
});

// ============================================================================
// REST API Endpoints
// ============================================================================

/**
 * GET /api/status
 * Get current system and battery status
 */
app.get('/api/status', (req, res) => {
  const status = telemetry.getStatus();
  res.json(status);
});

/**
 * GET /api/config
 * Get device configuration
 */
app.get('/api/config', (req, res) => {
  const cfg = config.getConfig();
  res.json(cfg);
});

/**
 * POST /api/config
 * Update device configuration
 */
app.post('/api/config', (req, res) => {
  try {
    const updated = config.updateConfig(req.body);
    res.json({ success: true, config: updated });

    // Broadcast config update event via WebSocket
    broadcastEvent('config_updated', updated);
  } catch (error) {
    res.status(400).json({ success: false, error: error.message });
  }
});

/**
 * GET /api/mqtt/config
 * Get MQTT configuration
 */
app.get('/api/mqtt/config', (req, res) => {
  const mqttConfig = config.getMqttConfig();
  res.json(mqttConfig);
});

/**
 * POST /api/mqtt/config
 * Update MQTT configuration
 */
app.post('/api/mqtt/config', (req, res) => {
  try {
    const updated = config.updateMqttConfig(req.body);
    res.json({ success: true, config: updated });

    // Broadcast event
    broadcastEvent('mqtt_config_updated', updated);
  } catch (error) {
    res.status(400).json({ success: false, error: error.message });
  }
});

/**
 * GET /api/mqtt/status
 * Get MQTT connection status
 */
app.get('/api/mqtt/status', (req, res) => {
  const status = config.getMqttStatus();
  res.json(status);
});

/**
 * GET /api/can/status
 * Get CAN bus statistics
 */
app.get('/api/can/status', (req, res) => {
  const status = can.getStatus();
  res.json(status);
});

/**
 * Code metrics monitoring endpoints
 */
app.get('/api/monitoring/runtime', (req, res) => {
  res.json(metrics.getRuntime());
});

app.get('/api/monitoring/event-bus', (req, res) => {
  res.json(metrics.getEventBus());
});

app.get('/api/monitoring/tasks', (req, res) => {
  res.json(metrics.getTasks());
});

app.get('/api/monitoring/modules', (req, res) => {
  res.json(metrics.getModules());
});

// Legacy compatibility routes
app.get('/api/metrics/runtime', (req, res) => {
  res.json(metrics.getRuntime());
});

app.get('/api/event-bus/metrics', (req, res) => {
  res.json(metrics.getEventBus());
});

app.get('/api/system/tasks', (req, res) => {
  res.json(metrics.getTasks());
});

app.get('/api/system/modules', (req, res) => {
  res.json(metrics.getModules());
});

/**
 * GET /api/uart/status
 * Get UART link statistics
 */
app.get('/api/uart/status', (req, res) => {
  const status = uart.getStatus();
  res.json(status);
});

/**
 * GET /api/history
 * Get historical battery data
 */
app.get('/api/history', (req, res) => {
  const limit = parseInt(req.query.limit) || 512;
  const historyData = history.getHistory(limit);
  res.json(historyData);
});

/**
 * GET /api/history/files
 * List archived history files
 */
app.get('/api/history/files', (req, res) => {
  const files = history.getArchiveFiles();
  res.json(files);
});

/**
 * GET /api/history/archive
 * Get archived history data
 */
app.get('/api/history/archive', (req, res) => {
  const filename = req.query.file || 'history_2024_01_15.csv';
  const limit = parseInt(req.query.limit) || 100;
  const data = history.getArchiveData(filename, limit);
  res.json(data);
});

/**
 * GET /api/history/download
 * Download history as CSV
 */
app.get('/api/history/download', (req, res) => {
  const filename = req.query.file || 'current';
  const csv = history.generateCSV(filename);

  res.setHeader('Content-Type', 'text/csv');
  res.setHeader('Content-Disposition', `attachment; filename="${filename}.csv"`);
  res.send(csv);
});

/**
 * GET /api/registers
 * Read BMS register catalog
 */
app.get('/api/registers', (req, res) => {
  const regs = registers.getRegisters();
  res.json({ registers: regs });
});

/**
 * POST /api/registers
 * Write/update BMS registers
 * Accepts: {300: value, 301: value} or [{address: 300, value: 123}]
 */
app.post('/api/registers', (req, res) => {
  try {
    const updates = req.body.registers || req.body;
    let updatesArray;

    if (Array.isArray(updates)) {
      updatesArray = updates;
    } else if (typeof updates === 'object') {
      // Convert object format {300: value, 301: value} to array format
      updatesArray = Object.entries(updates).map(([address, value]) => ({
        address: parseInt(address),
        value: value
      }));
    } else {
      updatesArray = [updates];
    }

    const result = registers.updateRegisters(updatesArray);

    if (result.success) {
      res.json({ success: true, updated: result.updated });
      broadcastEvent('registers_updated', result.updated);
    } else {
      res.status(400).json({ success: false, errors: result.errors });
    }
  } catch (error) {
    res.status(400).json({ success: false, error: error.message });
  }
});

/**
 * POST /api/ota
 * Parse multipart upload and simulate OTA programming
 */
app.post('/api/ota', (req, res) => {
  const contentType = req.headers['content-type'] || '';
  const boundaryMatch = contentType.match(/boundary=([^;]+)/i);

  if (!boundaryMatch) {
    return res.status(400).json({ status: 'error', message: 'Missing multipart boundary' });
  }

  const boundary = `--${boundaryMatch[1]}`;
  const chunks = [];

  req.on('data', (chunk) => chunks.push(chunk));
  req.on('end', () => {
    try {
      const buffer = Buffer.concat(chunks);
      const boundaryBuffer = Buffer.from(boundary);
      const start = buffer.indexOf(boundaryBuffer);
      if (start === -1) {
        return res.status(400).json({ status: 'error', message: 'Boundary not found' });
      }

      const headerEndSequence = Buffer.from('\r\n\r\n');
      let cursor = start + boundaryBuffer.length + 2; // Skip boundary + CRLF
      const headerEnd = buffer.indexOf(headerEndSequence, cursor);
      if (headerEnd === -1) {
        return res.status(400).json({ status: 'error', message: 'Multipart headers incomplete' });
      }

      const headerText = buffer.slice(cursor, headerEnd).toString('utf8');
      if (!/name="firmware"/i.test(headerText)) {
        return res.status(400).json({ status: 'error', message: 'Multipart field must be named firmware' });
      }

      cursor = headerEnd + headerEndSequence.length;
      const nextBoundary = Buffer.from(`\r\n${boundary}`);
      let dataEnd = buffer.indexOf(nextBoundary, cursor);
      if (dataEnd === -1) {
        return res.status(400).json({ status: 'error', message: 'Multipart terminator missing' });
      }

      const firmwareBuffer = buffer.slice(cursor, dataEnd);
      const bytes = firmwareBuffer.length;
      const crc32 = crypto.createHash('sha256').update(firmwareBuffer).digest('hex').slice(0, 8).toUpperCase();

      broadcastEvent('ota_ready', { bytes, crc32, filename: 'mock.bin' });

      res.json({
        status: 'ok',
        bytes,
        crc32,
        partition: 'ota_mock',
        version: 'mock-1.0.0',
        reboot_required: true
      });
    } catch (error) {
      console.error('Failed to parse OTA upload', error);
      res.status(500).json({ status: 'error', message: 'Unable to parse OTA payload' });
    }
  });
});

/**
 * POST /api/system/restart
 * Simulate restart orchestration
 */
app.post('/api/system/restart', (req, res) => {
  const target = (req.body && typeof req.body.target === 'string') ? req.body.target.toLowerCase() : 'bms';
  const delayMs = (req.body && Number.isFinite(req.body.delay_ms)) ? Number(req.body.delay_ms) : 750;

  const bmsAttempted = target !== 'gateway';
  const gatewayRestart = target === 'gateway' ? true : false;

  if (gatewayRestart) {
    broadcastEvent('system_restart', { target: 'gateway', delay_ms: delayMs });
    return res.status(202).json({
      status: 'scheduled',
      bms_attempted: false,
      bms_status: 'skipped',
      gateway_restart: true,
      delay_ms: delayMs
    });
  }

  broadcastEvent('system_restart', { target: 'bms', delay_ms: delayMs });
  res.json({
    status: 'scheduled',
    bms_attempted: bmsAttempted,
    bms_status: 'ok',
    gateway_restart: false,
    delay_ms: 0
  });
});

// ============================================================================
// WebSocket Servers
// ============================================================================

// Create separate WebSocket servers for each endpoint
const wssTelemetry = new WebSocket.Server({ noServer: true });
const wssEvents = new WebSocket.Server({ noServer: true });
const wssUart = new WebSocket.Server({ noServer: true });
const wssCan = new WebSocket.Server({ noServer: true });

// Track clients for each WebSocket
const telemetryClients = new Set();
const eventClients = new Set();
const uartClients = new Set();
const canClients = new Set();

// Handle WebSocket upgrade
server.on('upgrade', (request, socket, head) => {
  const pathname = new URL(request.url, `http://${request.headers.host}`).pathname;

  if (pathname === '/ws/telemetry') {
    wssTelemetry.handleUpgrade(request, socket, head, (ws) => {
      wssTelemetry.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/events') {
    wssEvents.handleUpgrade(request, socket, head, (ws) => {
      wssEvents.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/uart') {
    wssUart.handleUpgrade(request, socket, head, (ws) => {
      wssUart.emit('connection', ws, request);
    });
  } else if (pathname === '/ws/can') {
    wssCan.handleUpgrade(request, socket, head, (ws) => {
      wssCan.emit('connection', ws, request);
    });
  } else {
    socket.destroy();
  }
});

// Telemetry WebSocket
wssTelemetry.on('connection', (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[WebSocket /ws/telemetry] Client connected: ${clientIp}`);

  telemetryClients.add(ws);

  // Send initial telemetry
  ws.send(JSON.stringify(telemetry.getSnapshot()));

  ws.on('close', () => {
    console.log(`[WebSocket /ws/telemetry] Client disconnected: ${clientIp}`);
    telemetryClients.delete(ws);
  });

  ws.on('error', (error) => {
    console.error(`[WebSocket /ws/telemetry] Error: ${error.message}`);
    telemetryClients.delete(ws);
  });
});

// Events WebSocket
wssEvents.on('connection', (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[WebSocket /ws/events] Client connected: ${clientIp}`);

  eventClients.add(ws);

  ws.on('close', () => {
    console.log(`[WebSocket /ws/events] Client disconnected: ${clientIp}`);
    eventClients.delete(ws);
  });

  ws.on('error', (error) => {
    console.error(`[WebSocket /ws/events] Error: ${error.message}`);
    eventClients.delete(ws);
  });
});

// UART WebSocket
wssUart.on('connection', (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[WebSocket /ws/uart] Client connected: ${clientIp}`);

  uartClients.add(ws);

  ws.on('close', () => {
    console.log(`[WebSocket /ws/uart] Client disconnected: ${clientIp}`);
    uartClients.delete(ws);
  });

  ws.on('error', (error) => {
    console.error(`[WebSocket /ws/uart] Error: ${error.message}`);
    uartClients.delete(ws);
  });
});

// CAN WebSocket
wssCan.on('connection', (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[WebSocket /ws/can] Client connected: ${clientIp}`);

  canClients.add(ws);

  ws.on('close', () => {
    console.log(`[WebSocket /ws/can] Client disconnected: ${clientIp}`);
    canClients.delete(ws);
  });

  ws.on('error', (error) => {
    console.error(`[WebSocket /ws/can] Error: ${error.message}`);
    canClients.delete(ws);
  });
});

/**
 * Broadcast event to event WebSocket clients
 */
function broadcastEvent(eventType, data) {
  const message = JSON.stringify({
    type: eventType,
    data: data,
    timestamp: Date.now()
  });

  eventClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

/**
 * Broadcast telemetry to telemetry clients
 */
function broadcastTelemetry() {
  const snapshot = telemetry.getSnapshot();
  const message = JSON.stringify(snapshot);

  telemetryClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

/**
 * Broadcast UART frame to UART clients
 */
function broadcastUartFrame() {
  const frame = uart.generateFrame();

  uartClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      if (frame.raw) {
        client.send(JSON.stringify(frame.raw));
      }
      if (frame.decoded) {
        client.send(JSON.stringify(frame.decoded));
      }
    }
  });
}

/**
 * Broadcast CAN frame to CAN clients
 */
function broadcastCanFrame() {
  // Simulate CAN frame
  const frame = {
    timestamp_ms: Date.now(),
    id: Math.floor(Math.random() * 0x7FF),
    data: Array.from({ length: 8 }, () => Math.floor(Math.random() * 256)),
    dlc: 8
  };

  canClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(JSON.stringify(frame));
    }
  });
}

// ============================================================================
// Simulation Loop
// ============================================================================

/**
 * Update telemetry and broadcast at 1Hz
 */
setInterval(() => {
  telemetry.update();
  broadcastTelemetry();
}, 1000);

/**
 * Add history entry every 60 seconds
 */
setInterval(() => {
  const snapshot = telemetry.getSnapshot();
  history.addEntry(snapshot);
  console.log(`[History] Added entry (SOC: ${snapshot.state_of_charge_pct}%)`);
}, 60000);

/**
 * Simulate random events
 */
setInterval(() => {
  const events = [
    { type: 'info', message: 'System running normally' },
    { type: 'warning', message: 'Cell voltage difference detected' },
    { type: 'success', message: 'Configuration saved' }
  ];

  const randomEvent = events[Math.floor(Math.random() * events.length)];
  broadcastEvent('notification', randomEvent);
}, 30000);

/**
 * Broadcast UART frames at 2Hz
 */
setInterval(() => {
  broadcastUartFrame();
}, 500);

/**
 * Broadcast CAN frames at 10Hz
 */
setInterval(() => {
  broadcastCanFrame();
}, 100);

// ============================================================================
// Start Server
// ============================================================================

server.listen(PORT, () => {
  console.log('');
  console.log('='.repeat(60));
  console.log('  TinyBMS-GW Local Test Server');
  console.log('='.repeat(60));
  console.log('');
  console.log(`  🌐 Web Interface:  http://localhost:${PORT}`);
  console.log(`  📁 Web Directory:  ${WEB_DIR}`);
  console.log('');
  console.log('  WebSocket Endpoints:');
  console.log(`    📡 /ws/telemetry       - Battery telemetry (1Hz)`);
  console.log(`    📡 /ws/events          - System events`);
  console.log(`    📡 /ws/uart            - UART frames (2Hz)`);
  console.log(`    📡 /ws/can             - CAN frames (10Hz)`);
  console.log('');
  console.log('  REST API Endpoints:');
  console.log('    GET  /api/status            - System status');
  console.log('    GET  /api/config            - Device config');
  console.log('    POST /api/config            - Update config');
  console.log('    GET  /api/mqtt/config       - MQTT config');
  console.log('    POST /api/mqtt/config       - Update MQTT');
  console.log('    GET  /api/mqtt/status       - MQTT status');
  console.log('    GET  /api/uart/status       - UART stats');
  console.log('    GET  /api/history           - History data');
  console.log('    GET  /api/history/files     - Archive files');
  console.log('    GET  /api/history/download  - Download CSV');
  console.log('    GET  /api/registers         - BMS registers');
  console.log('    POST /api/registers         - Update registers');
  console.log('');
  console.log('  Press Ctrl+C to stop');
  console.log('='.repeat(60));
  console.log('');
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\n\nShutting down server...');
  server.close(() => {
    console.log('Server stopped');
    process.exit(0);
  });
});
