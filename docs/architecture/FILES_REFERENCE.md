# FICHIERS CLÉS - RÉFÉRENCE RAPIDE

## RÉSUMÉ PAR CATÉGORIE

### 1. PROTOCOLE MODBUS/UART BMS (59 registres, polling 250ms)

| Fichier | Type | Lignes | Contenu clé |
|---------|------|--------|-------------|
| `main/uart_bms/uart_bms_protocol.h` | Header | 1-147 | `uart_bms_register_id_t` enum (45 registres), `uart_bms_register_metadata_t` |
| `main/uart_bms/uart_bms_protocol.c` | Source | 1-577 | Table `g_uart_bms_registers[UART_BMS_REGISTER_COUNT]` avec tous les registres |
| `main/uart_bms/uart_bms.h` | Header | 1-128 | Structure `uart_bms_live_data_t` contenant toutes les données lues |
| `main/uart_bms/uart_bms.cpp` | Source | - | Implémentation UART comms C++ |
| `main/uart_bms/uart_frame_builder.h` | Header | - | Construction frames Modbus |
| `main/uart_bms/uart_frame_builder.cpp` | Source | - | Implémentation builder frames |
| `main/uart_bms/uart_response_parser.h` | Header | - | Parsing réponses Modbus |
| `main/uart_bms/uart_response_parser.cpp` | Source | - | Implémentation parser |

**Registres clés documentés** :
- 0x0000-0x000F: Voltages cellules (16 x UINT16, scale 0.1mV)
- 0x0024-0x0026: Pack voltage/current (FLOAT32)
- 0x002E: SOC haute précision (UINT32, scale 0.000001%)
- 0x0131-0x0140: Configuration batterie
- 0x01F4-0x01F6: Version HW/FW

---

### 2. PROTOCOLE CAN VICTRON (25 PGN, bitrate 250kbps)

| Fichier | Type | Lignes | Contenu clé |
|---------|------|--------|-------------|
| `main/can_victron/can_victron.h` | Header | 1-106 | Interface `can_victron_init()`, `can_victron_publish_frame()` |
| `main/can_victron/can_victron.c` | Source | 1-500+ | TWAI driver, keepalive (0x305), thread safety |
| `main/can_publisher/conversion_table.h` | Header | - | Interface conversion BMS → CAN |
| `main/can_publisher/conversion_table.c` | Source | 50-1400+ | **PGN definitions (lines 50-69)** + encoders |
| `main/can_publisher/can_publisher.h` | Header | 1-130 | `can_publisher_channel_t`, publisher registry |
| `main/can_publisher/can_publisher.c` | Source | - | Periodic scheduling et dispatcher |
| `main/can_publisher/cvl_controller.h` | Header | - | CVL (Charge Voltage Limit) logic |
| `main/can_publisher/cvl_types.h` | Header | 1-21 | `cvl_state_t` enum (BULK, FLOAT, etc) |
| `main/include/can_config_defaults.h` | Header | 1-75 | GPIO, bitrate, keepalive config |

**PGN Victron clés** :
- 0x307: Handshake "VIC"
- 0x351: CVL/CCL/DCL (limites charge/décharge)
- 0x355: SOC/SOH
- 0x356: Voltage/Current/Temperature
- 0x378: Energy Counters (Wh)
- 0x373: Cell Extremes (min/max V et T)

---

### 3. WEBSERVER REST API & WEBSOCKETS

| Fichier | Type | Lignes | Contenu clé |
|---------|------|--------|-------------|
| `main/web_server/web_server.h` | Header | 1-48 | **Endpoints list (lines 14-25)** et declaration |
| `main/web_server/web_server.c` | Source | 2609-2900+ | API handlers registration |
| `main/web_server/web_server_alerts.h` | Header | 1-78 | Alert API endpoints + `/ws/alerts` handler |
| `main/web_server/web_server_alerts.c` | Source | - | Alert API implementation |
| `web/src/js/utils/fetchAPI.js` | Script | 1-313 | Client-side API wrapper (GET, POST, retry logic) |
| `web/src/components/alerts/alerts.js` | Script | - | Alert UI component |
| `web/src/js/utils/canTooltips.js` | Script | - | CAN message tooltips/descriptions |

**Endpoints REST** :
- `GET /api/status` (2650-2655)
- `GET /api/config`, `POST /api/config` (2657-2671)
- `GET /api/metrics/runtime`, `/api/event-bus/metrics`
- `GET /api/system/tasks`, `POST /api/system/restart`
- `GET /api/can/status`
- `GET/POST /api/alerts/*`
- `POST /api/ota`

**WebSocket endpoints** :
- `/ws/telemetry` (2834-2841): 250ms updates
- `/ws/events` (2843-2850): Event notifications
- `/ws/uart` (2852-2859): UART raw data
- `/ws/can` (2862-2869): CAN frame monitoring
- `/ws/alerts` (2871-2878): Alert notifications

**Rate limiting** : 10 msg/sec per client, 32KB max payload

---

### 4. GESTION CONFIGURATION & MONITORING

| Fichier | Type | Contenu clé |
|---------|------|-------------|
| `main/config_manager/config_manager.h` | Header | Configuration management interface |
| `main/config_manager/config_manager.c` | Source | JSON serialization/deserialization |
| `main/monitoring/monitoring.h` | Header | Status snapshot, battery monitoring |
| `main/monitoring/monitoring.c` | Source | `monitoring_get_status_json()` |
| `main/alert_manager/alert_manager.h` | Header | Alert generation and management |
| `main/alert_manager/alert_manager.c` | Source | Alert triggering logic |
| `main/system_metrics/system_metrics.h` | Header | Runtime metrics collection |
| `main/system_metrics/system_metrics.c` | Source | Memory, tasks, CPU metrics |

---

### 5. INTÉGRATION & INFRASTRUCTURE

| Fichier | Type | Contenu clé |
|---------|------|-------------|
| `main/event_bus/event_bus.h` | Header | Event bus interface |
| `main/event_bus/event_bus.c` | Source | Publisher/subscriber pattern |
| `main/include/app_events.h` | Header | Event type definitions |
| `main/app_main.c` | Source | Initialization sequence, module setup |
| `main/mqtt_client/mqtt_client.h` | Header | MQTT client interface |
| `main/mqtt_gateway/mqtt_gateway.h` | Header | MQTT to internal data bridge |

---

## FLUX DE DONNÉES COMPLET

```
┌─────────────────┐
│  TinyBMS        │
│  (Batterie)     │ ──UART──→ uart_bms.cpp (UART polling 250ms)
└─────────────────┘           ↓ frames

uart_bms_protocol.c
(Parse 59 registres, 45 registered, scale values)
                               ↓ uart_bms_live_data_t

┌────────────────────────────────────────┐
│  ESP32 Gateway (Frontend)               │
│                                        │
│  conversion_table.c                    │
│  (Conversion BMS → CAN PGN)            │
│  - 25 encoders (0x307-0x382)          │
│  - Energy counters                     │
│  - CVL logic (cvl_controller.c)       │
│                   ↓ can_publisher_frame_t
│
│  can_victron.c (TWAI Driver)
│  - 250kbps bitrate, GPIO 7/6
│  - Keepalive 0x305 (1000ms)
│  - Rate limiting, mutex protection
│                   ↓ CAN frames
└────────────────────────────────────────┘

         ↓ CAN BUS (Victron format)

┌─────────────────────┐
│ Cerbo GX / System   │ (25 PGN reçus)
│ Victron Energy      │
└─────────────────────┘
```

---

## FORMAT DES DONNÉES - RÉSUMÉ

### Types Modbus
- **UINT16**: 0-65535, big-endian
- **INT16**: -32768 à +32767, complément à 2
- **UINT32**: Composé 2 mots, big-endian
- **FLOAT32**: IEEE 754, 2 mots big-endian
- **INT8_PAIR**: LSB (min) + MSB (max)

### Scaling
- Tension cellules: raw × 0.1 = mV
- Courants: raw × 0.1 = A
- Tensions: 1.0 = V (pour Pack V/I: FLOAT32)
- SOC: raw × 0.000001 = % (haute précision)
- SOH: raw × 0.002 = %

### Encodage CAN
- Entiers: Big-endian (Victron standard)
- Scaling stocké dans encodeurs
- Exemple CVL: (48.5V / 0.1) = 485 = 0x01E5

---

## POINTS D'INTÉGRATION CLÉS

### 1. Ajouter un nouveau registre Modbus
- Éditer `uart_bms_protocol.h` : ajouter enum `UART_BMS_REGISTER_XXX`
- Éditer `uart_bms_protocol.c` : ajouter entrée dans `g_uart_bms_registers[]`
- Mettre à jour `UART_BMS_REGISTER_WORD_COUNT` et `g_uart_bms_poll_addresses[]`

### 2. Ajouter un nouveau message CAN
- Éditer `conversion_table.c` : ajouter `#define VICTRON_PGN_XXX 0xYYYU`
- Implémenter encoder `static void conversion_encode_xxx()` 
- Ajouter channel au registry `conversion_get_channels()`

### 3. Ajouter un nouvel endpoint API
- Éditer `web_server.h` : documenter endpoint
- Éditer `web_server.c` : implémenter handler + enregistrer avec `httpd_register_uri_handler()`

### 4. Ajouter un WebSocket event
- Éditer `web_server.c` : ajouter type à `web_server_broadcast_*()` call
- Encoder JSON et envoyer via `ws_client_list_broadcast()`

---

## FICHIERS DOCUMENTATION

- `DOCUMENTATION_COMMUNICATIONS.md` - Documentation complète (ce fichier)
- `COMMUNICATION_REFERENCE.json` - Données structurées (JSON)
- `FILES_REFERENCE.md` - Référence rapide fichiers (ce fichier)
- `AUDIT_REPORT.md` - Audit frontend/backend
- `RAPPORT_AUDIT_FRONTEND_BACKEND.md` - Rapport alignement

---

## COMMANDES UTILES

### Rechercher un registre Modbus
```bash
grep -n "0x00YY" main/uart_bms/uart_bms_protocol.c
```

### Trouver un PGN CAN
```bash
grep -n "0xPGN" main/can_publisher/conversion_table.c
```

### Lister tous les endpoints API
```bash
grep -n "\.uri = " main/web_server/web_server.c
```

### Trouver les WebSocket handlers
```bash
grep -n "ws_handler\|/ws/" main/web_server/web_server.c
```

---

**Version** : 1.0  
**Date** : 2025-11-10  
**Projet** : TinyBMS-GW (ESP32 Victron Gateway)

