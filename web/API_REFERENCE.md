# API Reference - TinyBMS-GW

Documentation complète de l'API REST et WebSocket du TinyBMS Gateway.

---

## 📋 Table des Matières

- [Vue d'Ensemble](#vue-densemble)
- [Authentification](#authentification)
- [REST API](#rest-api)
- [WebSocket API](#websocket-api)
- [Codes d'Erreur](#codes-derreur)
- [Exemples](#exemples)

---

## 🎯 Vue d'Ensemble

### Base URL

```
http://<ESP32_IP>/
```

**Exemple:** `http://192.168.1.100/`

### Formats de Données

- **Request:** `application/json`
- **Response:** `application/json`
- **Encoding:** UTF-8

### Versioning

API version: **v1** (implicite, pas de préfixe `/v1/`)

---

## 🔐 Authentification

**Status:** Non implémenté (Phase 1.5)

Actuellement, aucune authentification requise.

**Future:** HTTP Basic Auth pour endpoints sensibles (`POST`, `DELETE`).

---

## 🌐 REST API

### System

#### GET /api/status

Retourne l'état complet du système.

**Response 200:**
```json
{
  "uptime_ms": 1234567,
  "free_heap": 45678,
  "min_heap": 40000,
  "task_count": 12,
  "wifi": {
    "connected": true,
    "ssid": "MyNetwork",
    "rssi": -45,
    "ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "netmask": "255.255.255.0"
  },
  "mqtt": {
    "connected": true,
    "broker": "mqtt://192.168.1.10:1883"
  },
  "battery": {
    "voltage_mv": 52000,
    "current_ma": -1500,
    "soc_percent": 75,
    "soh_percent": 98,
    "temperature_c": 25.5,
    "cells_count": 16,
    "status": "DISCHARGING"
  }
}
```

**Exemples:**
```bash
curl http://192.168.1.100/api/status | jq
```

---

#### GET /api/metrics/runtime

Métriques d'exécution système.

**Response 200:**
```json
{
  "uptime_s": 1234,
  "free_heap": 45678,
  "min_heap_ever": 40000,
  "largest_free_block": 32768,
  "tasks": {
    "total": 12,
    "running": 2,
    "blocked": 8,
    "suspended": 2
  },
  "cpu_usage_percent": 15
}
```

---

#### GET /api/system/tasks

Liste des tâches FreeRTOS.

**Response 200:**
```json
{
  "tasks": [
    {
      "name": "IDLE",
      "state": "Ready",
      "priority": 0,
      "stack_high_water_mark": 128
    },
    {
      "name": "mqtt_task",
      "state": "Blocked",
      "priority": 5,
      "stack_high_water_mark": 512
    }
  ]
}
```

---

### Configuration

#### GET /api/config

Récupère configuration complète.

**Response 200:**
```json
{
  "wifi": {
    "ssid": "MyNetwork",
    "static_ip": "",
    "gateway": "",
    "netmask": ""
  },
  "mqtt": {
    "broker_uri": "mqtt://192.168.1.10",
    "port": 1883,
    "username": "tinybms",
    "client_id": "tinybms-gw-001",
    "topics": {
      "status": "tinybms/status",
      "metrics": "tinybms/metrics",
      "config": "tinybms/config"
    }
  },
  "uart": {
    "baudrate": 115200,
    "tx_pin": 17,
    "rx_pin": 16,
    "timeout_ms": 1000
  },
  "can": {
    "speed": 500000,
    "tx_pin": 5,
    "rx_pin": 4,
    "protocol": "VICTRON"
  }
}
```

---

#### POST /api/config

Sauvegarde configuration.

**Request:**
```json
{
  "wifi_ssid": "NewNetwork",
  "wifi_password": "secret123",
  "mqtt_broker": "mqtt://broker.example.com",
  "mqtt_port": 1883
}
```

**Response 200:**
```json
{
  "success": true,
  "message": "Configuration saved, restarting..."
}
```

**Response 400:**
```json
{
  "error": "Invalid configuration",
  "details": "wifi_ssid too long (max 32 chars)"
}
```

---

### MQTT

#### GET /api/mqtt/config

Configuration MQTT détaillée.

**Response 200:**
```json
{
  "scheme": "mqtt",
  "broker_uri": "mqtt://192.168.1.10:1883",
  "host": "192.168.1.10",
  "port": 1883,
  "username": "tinybms",
  "password": "********",  // Masqué pour sécurité
  "client_cert_path": "",
  "ca_cert_path": "",
  "verify_hostname": false,
  "keepalive": 120,
  "default_qos": 1,
  "retain": false,
  "topics": {
    "status": "tinybms/status",
    "metrics": "tinybms/metrics",
    "config": "tinybms/config",
    "can_raw": "tinybms/can/raw",
    "can_decoded": "tinybms/can/decoded",
    "can_ready": "tinybms/can/ready"
  }
}
```

---

#### POST /api/mqtt/config

Modifier configuration MQTT.

**Request:**
```json
{
  "broker_uri": "mqtt://new-broker.com",
  "port": 1883,
  "username": "newuser",
  "password": "newpassword",
  "topic_status": "custom/status"
}
```

**Response 200:**
```json
{
  "success": true,
  "message": "MQTT configuration updated"
}
```

---

#### GET /api/mqtt/status

État connexion MQTT.

**Response 200:**
```json
{
  "connected": true,
  "broker": "mqtt://192.168.1.10:1883",
  "client_id": "tinybms-gw-001",
  "messages_sent": 1234,
  "messages_received": 567,
  "errors": 2,
  "last_error": ""
}
```

---

### Alerts

#### GET /api/alerts/active

Liste des alertes actives.

**Response 200:**
```json
{
  "alerts": [
    {
      "alert_id": 1,
      "type": 1,  // Temperature high
      "severity": 2,  // Critical
      "status": 0,  // Active
      "message": "Température élevée: 48°C > 45°C",
      "timestamp_ms": 1234567890
    },
    {
      "alert_id": 2,
      "type": 4,  // Cell voltage low
      "severity": 1,  // Warning
      "status": 0,
      "message": "Tension cellule basse: Cell 5 = 2.8V",
      "timestamp_ms": 1234567900
    }
  ],
  "count": 2
}
```

---

#### GET /api/alerts/history

Historique des alertes.

**Query Parameters:**
- `limit` (number): Nombre max d'alertes (défaut: 50)
- `offset` (number): Offset pour pagination

**Example:** `GET /api/alerts/history?limit=10&offset=0`

**Response 200:**
```json
{
  "alerts": [
    {
      "alert_id": 10,
      "type": 1,
      "severity": 2,
      "status": 1,  // Acknowledged
      "message": "Température haute",
      "timestamp_ms": 1234567000,
      "ack_timestamp_ms": 1234567100
    }
  ],
  "total": 150,
  "limit": 10,
  "offset": 0
}
```

---

#### GET /api/alerts/statistics

Statistiques alertes.

**Response 200:**
```json
{
  "total_alerts": 234,
  "active_alert_count": 2,
  "critical_count": 45,
  "warning_count": 120,
  "info_count": 69,
  "avg_acknowledgment_time_ms": 3456
}
```

---

#### GET /api/alerts/config

Configuration alertes.

**Response 200:**
```json
{
  "enabled": true,
  "debounce_sec": 60,
  "temp_high_enabled": true,
  "temperature_max_c": 45,
  "temp_low_enabled": false,
  "temperature_min_c": 0,
  "cell_volt_high_enabled": true,
  "cell_voltage_max_mv": 3650,
  "cell_volt_low_enabled": true,
  "cell_voltage_min_mv": 2800,
  "monitor_tinybms_events": true,
  "monitor_status_changes": true
}
```

---

#### POST /api/alerts/config

Modifier configuration alertes.

**Request:**
```json
{
  "enabled": true,
  "debounce_sec": 120,
  "temperature_max_c": 50,
  "cell_voltage_max_mv": 3700
}
```

**Response 200:**
```json
{
  "success": true
}
```

---

#### POST /api/alerts/acknowledge

Acquitter toutes les alertes actives.

**Response 200:**
```json
{
  "success": true,
  "acknowledged_count": 3
}
```

---

#### POST /api/alerts/acknowledge/:id

Acquitter alerte spécifique.

**Example:** `POST /api/alerts/acknowledge/5`

**Response 200:**
```json
{
  "success": true,
  "alert_id": 5
}
```

---

#### DELETE /api/alerts/history

Effacer historique des alertes.

**Response 200:**
```json
{
  "success": true,
  "deleted_count": 150
}
```

---

### CAN Bus

#### GET /api/can/status

État du bus CAN.

**Response 200:**
```json
{
  "enabled": true,
  "state": "RUNNING",
  "speed": 500000,
  "tx_pin": 5,
  "rx_pin": 4,
  "protocol": "VICTRON",
  "messages_sent": 12345,
  "messages_received": 6789,
  "errors": 5,
  "last_error": "BUS_OFF"
}
```

---

### Event Bus

#### GET /api/event-bus/metrics

Métriques Event Bus.

**Response 200:**
```json
{
  "total_publishes": 12345,
  "total_subscriptions": 15,
  "active_subscriptions": 12,
  "queue_length": 0,
  "queue_max": 50,
  "dropped_events": 2
}
```

---

## 🔌 WebSocket API

### Base URL

```
ws://<ESP32_IP>/ws/<endpoint>
```

### Endpoints Disponibles

1. `/ws/telemetry` - Données batterie temps réel
2. `/ws/events` - Événements système
3. `/ws/uart` - Trames UART TinyBMS
4. `/ws/can` - Trames CAN
5. `/ws/alerts` - Alertes temps réel

---

### ws://host/ws/telemetry

Stream des données batterie.

**Fréquence:** ~1 Hz

**Messages:**
```json
{
  "type": "battery_data",
  "timestamp_ms": 1234567890,
  "voltage_mv": 52000,
  "current_ma": -1500,
  "soc_percent": 75,
  "soh_percent": 98,
  "temperature_c": 25.5,
  "cells": [
    {
      "index": 0,
      "voltage_mv": 3250,
      "balancing": false
    },
    {
      "index": 1,
      "voltage_mv": 3248,
      "balancing": true
    }
  ],
  "status": "DISCHARGING",
  "protection": {
    "ov": false,
    "uv": false,
    "oc_charge": false,
    "oc_discharge": false,
    "temp_high": false,
    "temp_low": false
  }
}
```

**Exemple JavaScript:**
```javascript
const ws = new WebSocket('ws://192.168.1.100/ws/telemetry');

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log(`Battery: ${data.voltage_mv}mV, ${data.current_ma}mA, ${data.soc_percent}%`);
  updateDashboard(data);
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};

ws.onclose = () => {
  console.log('WebSocket closed, reconnecting...');
  setTimeout(connect, 5000);
};
```

---

### ws://host/ws/events

Stream des événements système.

**Fréquence:** Event-driven

**Messages:**
```json
{
  "type": "system_event",
  "event": "WIFI_CONNECTED",
  "timestamp_ms": 1234567890,
  "data": {
    "ssid": "MyNetwork",
    "ip": "192.168.1.100"
  }
}
```

**Types d'événements:**
- `WIFI_CONNECTED` - WiFi connecté
- `WIFI_DISCONNECTED` - WiFi déconnecté
- `MQTT_CONNECTED` - MQTT connecté
- `MQTT_DISCONNECTED` - MQTT déconnecté
- `BATTERY_STATUS_CHANGE` - Changement état batterie
- `ALERT_TRIGGERED` - Alerte déclenchée
- `CONFIG_SAVED` - Configuration sauvegardée
- `OTA_START` - OTA démarré
- `OTA_COMPLETE` - OTA terminé

---

### ws://host/ws/uart

Stream des trames UART TinyBMS.

**Fréquence:** ~5 Hz

**Messages:**
```json
{
  "type": "uart_frame",
  "timestamp_ms": 1234567890,
  "direction": "RX",
  "raw": "AA55900102030405...FF",
  "parsed": {
    "command": 0x90,
    "length": 64,
    "voltage_mv": 52000,
    "current_ma": -1500,
    "cells": [...]
  }
}
```

---

### ws://host/ws/can

Stream des trames CAN.

**Fréquence:** Variable

**Messages:**
```json
{
  "type": "can_frame",
  "timestamp_ms": 1234567890,
  "direction": "TX",
  "can_id": "0x351",
  "data": [0x12, 0x34, 0x56, 0x78],
  "dlc": 4,
  "decoded": {
    "voltage": 52.0,
    "current": -15.0,
    "soc": 75
  }
}
```

---

### ws://host/ws/alerts

Stream des alertes temps réel.

**Fréquence:** Event-driven

**Messages:**
```json
{
  "type": "alert",
  "alert": {
    "alert_id": 15,
    "type": 1,
    "severity": 2,
    "status": 0,
    "message": "Température élevée: 48°C",
    "timestamp_ms": 1234567890
  }
}
```

ou

```json
{
  "type": "alerts",
  "alerts": [
    {
      "alert_id": 15,
      "type": 1,
      "severity": 2,
      "status": 0,
      "message": "Température élevée",
      "timestamp_ms": 1234567890
    }
  ]
}
```

---

### WebSocket Client Ping/Pong

Tous les WebSockets supportent Ping/Pong pour keep-alive.

**Client → Server (PING):**
```javascript
ws.send(JSON.stringify({ type: 'ping' }));
```

**Server → Client (PONG):**
```json
{
  "type": "pong",
  "timestamp_ms": 1234567890
}
```

---

## ⚠️ Codes d'Erreur

### HTTP Status Codes

| Code | Signification | Description |
|------|---------------|-------------|
| 200 | OK | Requête réussie |
| 400 | Bad Request | Paramètres invalides |
| 401 | Unauthorized | Authentification requise (futur) |
| 403 | Forbidden | Accès interdit |
| 404 | Not Found | Ressource non trouvée |
| 413 | Payload Too Large | Payload > limite |
| 500 | Internal Server Error | Erreur serveur |
| 503 | Service Unavailable | Service temporairement indisponible |

### Error Response Format

```json
{
  "error": "Error message",
  "code": "ERROR_CODE",
  "details": "Additional details"
}
```

**Exemple:**
```json
{
  "error": "Invalid configuration",
  "code": "INVALID_CONFIG",
  "details": "wifi_ssid must be 1-32 characters"
}
```

---

## 📝 Exemples

### Fetch Battery Status

```javascript
async function getBatteryStatus() {
  try {
    const response = await fetch('http://192.168.1.100/api/status');
    const data = await response.json();

    console.log(`Battery: ${data.battery.soc_percent}%`);
    console.log(`Voltage: ${data.battery.voltage_mv / 1000}V`);
    console.log(`Current: ${data.battery.current_ma / 1000}A`);

    return data.battery;
  } catch (error) {
    console.error('Failed to fetch status:', error);
  }
}
```

---

### Save Configuration

```javascript
async function saveConfig(config) {
  try {
    const response = await fetch('http://192.168.1.100/api/config', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(config)
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const result = await response.json();
    console.log('Config saved:', result.message);

    return result;
  } catch (error) {
    console.error('Failed to save config:', error);
    throw error;
  }
}

// Usage
saveConfig({
  mqtt_broker: 'mqtt://new-broker.com',
  mqtt_port: 1883,
  wifi_ssid: 'NewNetwork'
});
```

---

### WebSocket Telemetry

```javascript
class TelemetryClient {
  constructor(host) {
    this.host = host;
    this.ws = null;
    this.reconnectTimeout = null;
  }

  connect() {
    this.ws = new WebSocket(`ws://${this.host}/ws/telemetry`);

    this.ws.onopen = () => {
      console.log('Telemetry connected');
      this.clearReconnect();
    };

    this.ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      this.handleData(data);
    };

    this.ws.onerror = (error) => {
      console.error('Telemetry error:', error);
    };

    this.ws.onclose = () => {
      console.log('Telemetry disconnected');
      this.scheduleReconnect();
    };
  }

  handleData(data) {
    console.log(`SOC: ${data.soc_percent}%, Voltage: ${data.voltage_mv}mV`);

    // Update UI
    updateBatteryDisplay(data);

    // Update charts
    addChartDataPoint(data);
  }

  scheduleReconnect() {
    if (this.reconnectTimeout) return;

    this.reconnectTimeout = setTimeout(() => {
      console.log('Reconnecting...');
      this.connect();
    }, 5000);
  }

  clearReconnect() {
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
  }

  disconnect() {
    this.clearReconnect();

    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }
}

// Usage
const telemetry = new TelemetryClient('192.168.1.100');
telemetry.connect();

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
  telemetry.disconnect();
});
```

---

### Acknowledge Alerts

```javascript
async function acknowledgeAlert(alertId) {
  try {
    const response = await fetch(
      `http://192.168.1.100/api/alerts/acknowledge/${alertId}`,
      { method: 'POST' }
    );

    if (response.ok) {
      console.log(`Alert ${alertId} acknowledged`);
      refreshAlerts();
    }
  } catch (error) {
    console.error('Failed to acknowledge alert:', error);
  }
}

async function acknowledgeAllAlerts() {
  try {
    const response = await fetch(
      'http://192.168.1.100/api/alerts/acknowledge',
      { method: 'POST' }
    );

    const result = await response.json();
    console.log(`${result.acknowledged_count} alerts acknowledged`);

  } catch (error) {
    console.error('Failed to acknowledge alerts:', error);
  }
}
```

---

## 🔗 Ressources

- [Web Interface README](README.md)
- [Integration Guide](INTEGRATION_GUIDE.md)
- [Backend API (C)](../main/web_server/web_server.c)

---

**Version:** 1.0.0
**Dernière mise à jour:** 2025-01-09
