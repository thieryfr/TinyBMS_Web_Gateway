# DOCUMENTATION COMPLÈTE - COMMUNICATIONS ET PROTOCOLES
## TinyBMS-GW (Gateway TinyBMS vers Victron)

**Date** : 2025-11-10  
**Version** : 1.0  
**Objectif** : Documentation centralisée de tous les protocoles de communication du projet

---

## TABLE DES MATIÈRES

1. [Registres Modbus/UART BMS](#1-registres-modbusuat-bms)
2. [CAN ID Victron](#2-can-id-victron)
3. [APIs REST et WebSocket](#3-apis-rest-et-websocket)
4. [Fichiers Sources Pertinents](#4-fichiers-sources-pertinents)
5. [Format des Données](#5-formats-des-données)

---

## 1. REGISTRES MODBUS/UART BMS

### Résumé général
- **Protocole** : Modbus RTU sur UART
- **Nombre de registres** : 59 mots 16-bit (addresses 0x0000 à 0x01FF)
- **Baud rate** : 115200
- **Interval de polling** : 250 ms (configurable 100-1000 ms)
- **Timeout réponse** : 200 ms

### Tableau complet des registres

#### A. TENSIONS DE CELLULES
| Registre | Adresse | Type | Échelle | Unité | Description | Source |
|----------|---------|------|---------|-------|-------------|--------|
| Cell Voltage 01-16 | 0x0000-0x000F | UINT16 | 0.1 | mV | 16 tensions individuelles | uart_bms.h:76 |

#### B. DONNÉES PRIMAIRES
| Registre | Adresse | Mots | Type | Échelle | Unité | Description | Source |
|----------|---------|------|------|---------|-------|-------------|--------|
| Lifetime Counter | 0x0020 | 2 | UINT32 | 1.0 | s | Uptime du BMS | uart_bms.h:52 |
| Estimated Time Left | 0x0022 | 2 | UINT32 | 1.0 | s | Temps avant épuisement | uart_bms.h:53 |
| Pack Voltage | 0x0024 | 2 | FLOAT32 | 1.0 | V | Tension totale pack | uart_bms.h:41 |
| Pack Current | 0x0026 | 2 | FLOAT32 | 1.0 | A | Courant pack (>0=charge) | uart_bms.h:42 |
| Min Cell Voltage | 0x0028 | 1 | UINT16 | 1.0 | mV | Min des 16 cellules | uart_bms.h:43 |
| Max Cell Voltage | 0x0029 | 1 | UINT16 | 1.0 | mV | Max des 16 cellules | uart_bms.h:44 |
| External Temp #1 | 0x002A | 1 | INT16 | 0.1 | °C | Capteur externe 1 | uart_bms.h:47 |
| External Temp #2 | 0x002B | 1 | INT16 | 0.1 | °C | Capteur externe 2 | uart_bms.h:55 |
| State of Health | 0x002D | 1 | UINT16 | 0.002 | % | SOH (0-100%) précision 0.002% | uart_bms.h:46 |
| State of Charge | 0x002E | 2 | UINT32 | 0.000001 | % | SOC haute précision (0.0001%) | uart_bms.h:45 |
| Internal Temperature | 0x0030 | 1 | INT16 | 0.1 | °C | Température MOS/électronique | uart_bms.h:48 |
| System Status | 0x0032 | 1 | UINT16 | 1.0 | - | 0=offline, 1=online | uart_bms.h:50 |
| Need Balancing | 0x0033 | 1 | UINT16 | 1.0 | - | Flag équilibrage demandé | uart_bms.h:49 |
| Real Balancing Bits | 0x0034 | 1 | UINT16 | 1.0 | - | Bits d'équilibrage actifs | uart_bms.h:49 |

#### C. LIMITES DYNAMIQUES DE COURANT
| Registre | Adresse | Mots | Type | Échelle | Unité | Description | Source |
|----------|---------|------|------|---------|-------|-------------|--------|
| Max Discharge Current | 0x0066 | 1 | UINT16 | 0.1 | A | Courant décharge max autorisé | uart_bms.h:64 |
| Max Charge Current | 0x0067 | 1 | UINT16 | 0.1 | A | Courant charge max autorisé | uart_bms.h:65 |
| Pack Temp Min/Max | 0x0071 | 1 | INT8_PAIR | 1.0 | °C | Min (LSB), Max (MSB) | uart_bms.h:56 |

#### D. CONFIGURATION STOCKÉE
| Registre | Adresse | Mots | Type | Échelle | Unité | Description | Source |
|----------|---------|------|------|---------|-------|-------------|--------|
| Peak Discharge Current Cutoff | 0x0131 | 1 | UINT16 | 1.0 | A | Limite pic décharge | uart_bms_protocol.c:405-412 |
| Battery Capacity | 0x0132 | 1 | UINT16 | 0.01 | Ah | Capacité nominale batterie | uart_bms_protocol.c:415-425 |
| Series Cell Count | 0x0133 | 1 | UINT16 | 1.0 | cell | Nombre de cellules en série | uart_bms_protocol.c:427-437 |
| Overvoltage Cutoff | 0x013B | 1 | UINT16 | 1.0 | mV | Seuil surtension par cellule | uart_bms_protocol.c:439-449 |
| Undervoltage Cutoff | 0x013C | 1 | UINT16 | 1.0 | mV | Seuil sous-tension par cellule | uart_bms_protocol.c:451-461 |
| Discharge Over-current Cutoff | 0x013D | 1 | UINT16 | 1.0 | A | Seuil surcourant décharge | uart_bms_protocol.c:463-473 |
| Charge Over-current Cutoff | 0x013E | 1 | UINT16 | 1.0 | A | Seuil surcourant charge | uart_bms_protocol.c:475-485 |
| Overheat Cutoff | 0x013F | 1 | INT16 | 1.0 | °C | Seuil température trop haute | uart_bms_protocol.c:487-497 |
| Low Temp Charge Cutoff | 0x0140 | 1 | INT16 | 1.0 | °C | Seuil température trop basse | uart_bms_protocol.c:499-509 |

#### E. VERSION ET IDENTIFICATION
| Registre | Adresse | Mots | Type | Échelle | Unité | Description | Source |
|----------|---------|------|------|---------|-------|-------------|--------|
| Hardware/Changes Version | 0x01F4 | 1 | UINT16 | 1.0 | - | HW Ver (LSB), Changes Ver (MSB) | uart_bms_protocol.c:511-521 |
| Public Firmware/Flags | 0x01F5 | 1 | UINT16 | 1.0 | - | FW Ver (LSB), Flags (MSB) | uart_bms_protocol.c:523-533 |
| Internal Firmware Version | 0x01F6 | 1 | UINT16 | 1.0 | - | Version interne (propriétaire) | uart_bms_protocol.c:535-545 |

### Structures de données C

```c
// Voir uart_bms.h lignes 24-80
typedef struct {
    uint64_t timestamp_ms;
    float pack_voltage_v;
    float pack_current_a;
    uint16_t min_cell_mv;
    uint16_t max_cell_mv;
    float state_of_charge_pct;
    float state_of_health_pct;
    float average_temperature_c;
    float mosfet_temperature_c;
    uint16_t balancing_bits;
    uint16_t alarm_bits;
    uint16_t warning_bits;
    uint32_t uptime_seconds;
    uint32_t estimated_time_left_seconds;
    uint32_t cycle_count;
    float auxiliary_temperature_c;
    float pack_temperature_min_c;
    float pack_temperature_max_c;
    float battery_capacity_ah;
    uint16_t series_cell_count;
    // ... et plus de champs
    uint16_t cell_voltage_mv[UART_BMS_CELL_COUNT];
    uint8_t cell_balancing[UART_BMS_CELL_COUNT];
    uart_bms_register_entry_t registers[UART_BMS_MAX_REGISTERS];
} uart_bms_live_data_t;
```

### Adresses de polling (ordre)
```c
// Voir uart_bms_protocol.c:550-558
Adresses polling 59 mots:
0x0000-0x000F (16) : Voltages cellules
0x0020-0x0023 (4)  : Lifetime & Estimated time
0x0024-0x0027 (4)  : Pack V/I (FLOAT)
0x0028-0x002B (4)  : Min/Max Cell V, Ext Temp
0x002D-0x002F (3)  : SOH, SOC
0x0030 (1)         : Internal Temp
0x0032-0x0034 (3)  : Status, Balancing flags
0x0066-0x0067 (2)  : Max Current limits
0x0071 (1)         : Pack Temp Min/Max
0x0131-0x0140 (16) : Configuration params
0x01F4-0x01F6 (3)  : HW/FW versions
0x01F7-0x01FF (9)  : Reserved/extended polling
```

---

## 2. CAN ID VICTRON

### Configuration CAN générale
- **Bitrate** : 500 000 bps
- **Mode** : Standard Frame (11-bit)
- **GPIO TX** : GPIO 7
- **GPIO RX** : GPIO 6
- **Keepalive ID** : 0x305U
- **Priorité logique** : 6
- **Source Address (données)** : 0xE5U

### Encodage ID (standard 11-bit)
- Chaque trame utilise directement le PGN Victron (0x305–0x382) comme identifiant 11 bits.
- Les bits de priorité (6) et l'adresse source (0xE5) restent documentés dans la charge utile et l'ordre d'envoi.
- Aucun encapsulage 29 bits n'est désormais requis.

### Tableau complet des trames (Victron)

| CAN ID (11-bit) | Référence PGN | Nom (Fonction) | Contenu/Structure | DLC | Fréquence d'envoi | Source fichier |
|-----------------|----------------|----------------|-------------------|-----|-------------------|----------------|
| 0x305 | Keepalive | Keepalive | 1 byte 0x00 | 1 | 1000 ms | can_victron.c:29 |
| 0x307 | 0x307 | Handshake | "VIC" + info identité | 3 | Connexion initiale | conversion_table.c:50 |
| 0x351 | 0x351 | CVL/CCL/DCL | Charge Voltage Limit (2B), Charge Current Limit (2B), Discharge Current Limit (2B) | 8 | 100 ms (config) | conversion_table.c:51 |
| 0x355 | 0x355 | SOC/SOH | State of Charge (2B uint16, 0-10000=0-100%), State of Health (2B) | 8 | 1000 ms | conversion_table.c:52 |
| 0x356 | 0x356 | Voltage/Current | Voltage (2B signed), Current (2B signed), Temperature (2B) | 8 | 1000 ms | conversion_table.c:53 |
| 0x35A | 0x35A | Alarms | Bits d'alarme (8 bytes) | 8 | À la modification | conversion_table.c:54 |
| 0x35E | 0x35E | Manufacturer | Manufacturer ID (ASCII/bytes) | 8 | Connexion | conversion_table.c:55 |
| 0x35F | 0x35F | Battery Info | Model ID (2B), Firmware (2B), Capacity (2B) | 8 | Connexion | conversion_table.c:56 |
| 0x370 | 0x370 | BMS Name Part 1 | Nom batterie bytes [0:7] (ASCII) | 8 | Connexion | conversion_table.c:57 |
| 0x371 | 0x371 | BMS Name Part 2 | Nom batterie bytes [8:15] (ASCII) | 8 | Connexion | conversion_table.c:58 |
| 0x372 | 0x372 | Module Status | État des modules | 8 | 1000 ms | conversion_table.c:59 |
| 0x373 | 0x373 | Cell Extremes | Min/Max cell voltages, Min/Max temperatures | 8 | 1000 ms | conversion_table.c:60 |
| 0x374 | 0x374 | Min Cell ID | Cell ID avec voltage min (0-15) | 2 | 1000 ms | conversion_table.c:61 |
| 0x375 | 0x375 | Max Cell ID | Cell ID avec voltage max (0-15) | 2 | 1000 ms | conversion_table.c:62 |
| 0x376 | 0x376 | Min Temp ID | ID source température min | 2 | 1000 ms | conversion_table.c:63 |
| 0x377 | 0x377 | Max Temp ID | ID source température max | 2 | 1000 ms | conversion_table.c:64 |
| 0x378 | 0x378 | Energy Counters | Énergie chargée (4B), déchargée (4B) en Wh | 8 | 10 000 ms | conversion_table.c:65 |
| 0x379 | 0x379 | Installed Capacity | Capacité nominale (Ah) | 8 | Connexion | conversion_table.c:66 |
| 0x380 | 0x380 | Serial Part 1 | Série bytes [0:7] (ASCII) | 8 | Connexion | conversion_table.c:67 |
| 0x381 | 0x381 | Serial Part 2 | Série bytes [8:15] (ASCII) | 8 | Connexion | conversion_table.c:68 |
| 0x382 | 0x382 | Battery Family | Famille batterie (texte) | 8 | Connexion | conversion_table.c:69 |

### Message Keepalive
```c
// Voir can_victron.c:29-30
#define CAN_VICTRON_KEEPALIVE_ID         0x305U
#define CAN_VICTRON_KEEPALIVE_DLC        1U
```
- **Interval** : 1000 ms (CONFIG_TINYBMS_CAN_KEEPALIVE_INTERVAL_MS)
- **Timeout** : 10000 ms
- **Retry** : 500 ms
- **Payload** : 1 byte (0x00)

### Encodage des données CAN (examples)

#### CVL/CCL/DCL (0x351)
```c
// Voir conversion_table.c:860-869
uint16_t cvl_raw = encode_u16_scaled(cvl_v, 10.0f, 0.0f, 0U, 0xFFFFU);
uint16_t ccl_raw = encode_u16_scaled(ccl_a, 10.0f, 0.0f, 0U, 0xFFFFU);
uint16_t dcl_raw = encode_u16_scaled(dcl_a, 10.0f, 0.0f, 0U, 0xFFFFU);
frame->data[0] = (uint8_t)(cvl_raw & 0xFFU);
frame->data[1] = (uint8_t)((cvl_raw >> 8U) & 0xFFU);
frame->data[2] = (uint8_t)(ccl_raw & 0xFFU);
frame->data[3] = (uint8_t)((ccl_raw >> 8U) & 0xFFU);
frame->data[4] = (uint8_t)(dcl_raw & 0xFFU);
frame->data[5] = (uint8_t)((dcl_raw >> 8U) & 0xFFU);
// Échelle: 0.1 V/A, byte order: little-endian
```

---

## 3. APIS REST ET WEBSOCKET

### Endpoints REST Complets

#### Base URL
```
http://<device_ip>/api
```

#### Système et Status
```
GET  /api/status              - État global système
GET  /api/config              - Configuration actuelle
POST /api/config              - Mettre à jour configuration
GET  /api/metrics/runtime     - Métriques runtime système
GET  /api/event-bus/metrics   - Statistiques bus événements
GET  /api/system/tasks        - Liste tasks FreeRTOS
GET  /api/system/modules      - État des modules
POST /api/system/restart      - Redémarrer (body: {"target":"gateway"})
```

#### CAN Bus
```
GET  /api/can/status          - État CAN (TX/RX frames, errors)
```

#### Gestion Alertes
```
GET  /api/alerts/config       - Configuration des alertes
POST /api/alerts/config       - Mettre à jour config alertes
GET  /api/alerts/active       - Alertes actives
GET  /api/alerts/history?limit=N - Historique (défaut 100)
POST /api/alerts/acknowledge/{id} - Valider une alerte
POST /api/alerts/acknowledge  - Valider toutes
GET  /api/alerts/statistics   - Stats
DELETE /api/alerts/history    - Effacer historique
```

#### MQTT
```
GET  /api/mqtt/config         - Configuration MQTT
POST /api/mqtt/config         - Mettre à jour MQTT
GET  /api/mqtt/status         - État MQTT
GET  /api/mqtt/test           - Test connexion
```

#### OTA et Firmware
```
POST /api/ota                 - Upload firmware
    Body: multipart/form-data
    Field name: "firmware"
    Content-Type: application/octet-stream
```

### WebSockets

#### /ws/telemetry
- **URI** : `ws://<device_ip>/ws/telemetry`
- **Type de messages** : Télémétrie BMS en temps réel
- **Fréquence** : À chaque mise à jour BMS (250 ms par défaut)
- **Rate limit** : 10 msg/sec par client
- **Payload max** : 32 KB

Message type:
```json
{
  "type": "telemetry",
  "timestamp_ms": 1234567890,
  "pack_voltage_v": 48.5,
  "pack_current_a": 10.2,
  "state_of_charge_pct": 85.5,
  "state_of_health_pct": 98.2,
  "min_cell_mv": 3080,
  "max_cell_mv": 3120,
  "average_temperature_c": 25.3,
  "mosfet_temperature_c": 28.1,
  "auxiliary_temperature_c": 22.5,
  "cell_voltages_mv": [3080, 3085, 3090, ...],
  "cell_balancing": [0, 0, 1, 0, ...],
  "balancing_bits": 0x0004,
  "estimated_time_left_seconds": 36000,
  "uptime_seconds": 86400,
  "max_discharge_current_a": 100.0,
  "max_charge_current_a": 50.0
}
```

#### /ws/events
- **URI** : `ws://<device_ip>/ws/events`
- **Type de messages** : Événements système
- **Déclenchement** : À chaque événement système
- **Rate limit** : 10 msg/sec par client

Message type:
```json
{
  "type": "event",
  "event_id": "can_frame_sent",
  "key": "can.frame.sent",
  "timestamp_ms": 1234567890,
  "label": "CAN frame sent: CVL_CCL_DCL (0x351)",
  "data": {
    "pgn": "0x351",
    "can_id": "0x351",
    "dlc": 8,
    "payload": "XX XX XX XX XX XX XX XX"
  }
}
```

Types d'événements courants:
- `app.startup` - Démarrage application
- `bms.update` - Mise à jour données BMS
- `can.frame.sent` - Trame CAN envoyée
- `can.frame.received` - Trame CAN reçue
- `mqtt.connected` - MQTT connecté
- `mqtt.disconnected` - MQTT déconnecté
- `alert.triggered` - Alerte déclenchée
- `ota.upload_ready` - Firmware prêt OTA
- `system.restart` - Redémarrage système

#### /ws/uart
- **URI** : `ws://<device_ip>/ws/uart`
- **Type de messages** : Données UART brutes TinyBMS
- **Message initial** : `{"type":"uart","status":"connected"}`
- **Rate limit** : 10 msg/sec par client

Message type:
```json
{
  "type": "uart_data",
  "timestamp_ms": 1234567890,
  "bytes": "XX XX XX XX XX XX XX XX"
}
```

#### /ws/can
- **URI** : `ws://<device_ip>/ws/can`
- **Type de messages** : Trames CAN envoyées/reçues
- **Fréquence** : À chaque trame CAN (100-1000 ms selon config)
- **Rate limit** : 10 msg/sec par client

Message type:
```json
{
  "type": "can",
  "timestamp_ms": 1234567890,
  "can_id": "0x1851FEE5",
  "dlc": 8,
  "data": "XX XX XX XX XX XX XX XX",
  "description": "CVL_CCL_DCL"
}
```

#### /ws/alerts
- **URI** : `ws://<device_ip>/ws/alerts`
- **Type de messages** : Alertes et notifications
- **Déclenchement** : À chaque changement d'alerte
- **Rate limit** : 10 msg/sec par client

Message type:
```json
{
  "type": "alert",
  "id": "alert_12345",
  "timestamp_ms": 1234567890,
  "level": "ERROR",
  "title": "Overvoltage Detected",
  "message": "Cell 5 voltage exceeded 3.5V (actual: 3.52V)",
  "component": "BMS",
  "acknowledged": false
}
```

Niveaux d'alerte: `INFO`, `WARNING`, `ERROR`, `CRITICAL`

### Format de réponse JSON

#### /api/status
```json
{
  "bms": {
    "connected": true,
    "voltage_v": 48.5,
    "current_a": 10.2,
    "soc_pct": 85.5,
    "soh_pct": 98.2,
    "uptime_seconds": 86400
  },
  "can": {
    "bus_state": "RUNNING",
    "tx_frames": 1234,
    "rx_frames": 567,
    "errors": 0,
    "occupancy_pct": 5.2
  },
  "system": {
    "uptime_ms": 86400000,
    "memory_free_bytes": 524288,
    "temperature_c": 35.2,
    "tasks_running": 12
  }
}
```

#### /api/config
```json
{
  "demo": false,
  "mqtt": {
    "enabled": true,
    "broker": "192.168.1.100",
    "port": 1883,
    "username": "mqtt_user",
    "password": "***",
    "topic_prefix": "tinybms"
  },
  "can": {
    "enabled": true,
    "bitrate": 250000,
    "keepalive_interval_ms": 1000,
    "keepalive_timeout_ms": 10000
  },
  "wifi": {
    "ssid": "MyNetwork",
    "security": "WPA2"
  },
  "uart": {
    "poll_interval_ms": 250
  }
}
```

### Sécurité API
- Password toujours masquée en réponse (GET /api/config)
- Validation stricte JSON
- Rate limiting WebSocket (10 msg/sec)
- Payload max 32 KB
- Multipart handling pour OTA

---

## 4. FICHIERS SOURCES PERTINENTS

### Registres Modbus (UART BMS)
| Fichier | Lignes | Contenu |
|---------|--------|---------|
| `/home/user/TinyBMS-GW/main/uart_bms/uart_bms_protocol.h` | 1-147 | Énumérations registres, métadonnées |
| `/home/user/TinyBMS-GW/main/uart_bms/uart_bms_protocol.c` | 1-577 | Implémentation table registres (45 registres) |
| `/home/user/TinyBMS-GW/main/uart_bms/uart_bms.h` | 24-80 | Structure uart_bms_live_data_t |
| `/home/user/TinyBMS-GW/main/uart_bms/uart_frame_builder.h` | - | Builder frames UART |
| `/home/user/TinyBMS-GW/main/uart_bms/uart_response_parser.h` | - | Parser réponses UART |

### CAN Victron
| Fichier | Lignes | Contenu |
|---------|--------|---------|
| `/home/user/TinyBMS-GW/main/can_victron/can_victron.h` | 1-106 | Interface CAN driver |
| `/home/user/TinyBMS-GW/main/can_victron/can_victron.c` | 1-100 | Implémentation driver TWAI |
| `/home/user/TinyBMS-GW/main/can_publisher/conversion_table.c` | 50-69 | PGN Victron definitions |
| `/home/user/TinyBMS-GW/main/can_publisher/conversion_table.c` | 325-900+ | Encodeurs PGN |
| `/home/user/TinyBMS-GW/main/can_publisher/can_publisher.h` | 1-130 | Interface publisher |
| `/home/user/TinyBMS-GW/main/include/can_config_defaults.h` | 1-75 | Configuration defaults CAN |

### APIs Web
| Fichier | Lignes | Contenu |
|---------|--------|---------|
| `/home/user/TinyBMS-GW/main/web_server/web_server.h` | 1-48 | Endpoints API (documentation) |
| `/home/user/TinyBMS-GW/main/web_server/web_server.c` | 2609-2700+ | Déclaration handlers API |
| `/home/user/TinyBMS-GW/main/web_server/web_server.c` | 2834-2871 | Déclaration handlers WebSocket |
| `/home/user/TinyBMS-GW/main/web_server/web_server_alerts.h` | 1-78 | Handlers alertes API + WS |
| `/home/user/TinyBMS-GW/web/src/js/utils/fetchAPI.js` | 1-313 | Client API JavaScript |

### Configuration
| Fichier | Contenu |
|---------|---------|
| `/home/user/TinyBMS-GW/main/config_manager/config_manager.h` | Gestion configuration |
| `/home/user/TinyBMS-GW/main/monitoring/monitoring.h` | Monitoring et métriques |

---

## 5. FORMATS DES DONNÉES

### Type Encodage Entiers (Modbus)

#### UINT16 (Non-signé 16-bit)
- Range: 0 à 65535
- Byte order: Big-endian (Modbus)
- Exemple: 0x1234 = (0x12 * 256) + 0x34 = 4660

#### INT16 (Signé 16-bit)
- Range: -32768 à 32767
- Représentation: Complément à 2
- Byte order: Big-endian

#### UINT32 (Non-signé 32-bit)
- Range: 0 à 4,294,967,295
- Composé de 2 mots (4 bytes)
- Byte order: Big-endian

#### FLOAT32 (IEEE 754)
- Range: ±3.4 × 10^38
- Composé de 2 mots (4 bytes)
- Byte order: Big-endian
- Utilisé pour voltage pack et courant

#### INT8_PAIR (2 bytes, chacun signé)
- LSB (Low byte): température min (-128 à +127°C)
- MSB (High byte): température max (-128 à +127°C)
- Utilisé pour pack temperature min/max

### Encodage CAN Victron

#### Entiers 16-bit (Big-endian)
```c
uint16_t value = 0x1234;
frame->data[0] = (uint8_t)((value >> 8) & 0xFF);   // MSB
frame->data[1] = (uint8_t)(value & 0xFF);          // LSB
```

#### Valeurs avec Échelle
```c
// Exemple: 48.5 V avec échelle 0.1 V
uint16_t raw = (uint16_t)(48.5 / 0.1) = 485;
// Encoding: 485 = 0x01E5
frame->data[0] = 0x01;
frame->data[1] = 0xE5;
```

#### Tableaux ASCII (Strings)
- Jusqu'à 8 bytes par frame CAN
- Padding avec 0x00 si nécessaire
- Utilisé pour: Serial, Name, Manufacturer

---

## RÉSUMÉ INTEGRATION

### Flux Données Typique
```
TinyBMS (UART)
    ↓ (Modbus RTU, 250ms polling)
ESP32 (Gateway)
    ↓ (UART Parser)
uart_bms_live_data_t
    ↓ (Conversion)
CAN PGN Victron (25+ messages)
    ↓ (CAN TWAI Driver)
Cerbo GX / Victron System
```

### Flux API Typique
```
Client Web
    ↓ (HTTP/WebSocket)
ESP32 Web Server (HTTP 80)
    ↓ (JSON parsing/encoding)
Modules Backend (BMS, CAN, MQTT, etc)
    ↓ (Event Bus)
WebSocket Broadcast
    ↓
Tous clients connectés
```

---

## REFERENCES

- TinyBMS UART Protocol: `/home/user/TinyBMS-GW/main/uart_bms/uart_bms_protocol.c`
- Victron CANopen Spec: Implémentée dans `/home/user/TinyBMS-GW/main/can_publisher/conversion_table.c`
- ESP32 TWAI Driver: Utilisé dans `/home/user/TinyBMS-GW/main/can_victron/can_victron.c`
- Web Server (ESP-IDF): `/home/user/TinyBMS-GW/main/web_server/web_server.c`

