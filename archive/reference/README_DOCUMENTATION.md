# ANALYSE COMPLÈTE DES COMMUNICATIONS - TinyBMS-GW

Bienvenue ! Vous trouverez ici une **documentation exhaustive** de tous les protocoles de communication du projet TinyBMS-GW.

## Fichiers de Documentation Générés

### 1. **DOCUMENTATION_COMMUNICATIONS.md** (21 KB)
📖 **Documentation complète et détaillée**
- Tableau complet des **45 registres Modbus** avec adresses, types, échelles
- Tableau des **19 PGN Victron** avec CAN IDs et encodages
- Tous les **endpoints REST API** (15+ endpoints)
- Tous les **WebSocket** (5 endpoints avec exemples JSON)
- Formats de données et structures C
- Formules de conversion et scaling

**À consulter pour :** Comprendre chaque registre, chaque message CAN, chaque API en détail

---

### 2. **COMMUNICATION_REFERENCE.json** (15 KB)
📊 **Données structurées en JSON**
```json
{
  "modbus_registers": {
    "protocol": "Modbus RTU over UART",
    "total_registers": 45,
    "categories": { ... }
  },
  "can_messages": {
    "protocol": "Victron J1939-like",
    "pgn_table": [ ... ]
  },
  "rest_api": { ... },
  "websockets": { ... }
}
```

**À utiliser pour :** 
- Parsing programmatique
- Génération de documentation
- Intégration avec d'autres outils

---

### 3. **FILES_REFERENCE.md** (8.9 KB)
⚡ **Référence rapide des fichiers source**
- Résumé par catégorie (Modbus, CAN, Web API)
- Mapping fichiers ↔ contenu clé
- Flux de données complet avec diagramme
- Points d'intégration clés
- Commandes utiles pour recherches

**À consulter pour :** 
- Trouver rapidement un fichier
- Comprendre la structure du projet
- Naviguer dans le code source

---

### 4. **ANALYSIS_SUMMARY.txt** (11 KB)
📋 **Résumé textuel complet**
- Condensé de toutes les informations
- Chemins absolus des fichiers
- Résumé des données clés
- Points de démarrage pour modifications

**À consulter pour :** 
- Vue d'ensemble rapide
- Points de démarrage
- Résumé des adresses clés

---

## Tableau Récapitulatif

| Aspect | Nombre | Références |
|--------|--------|------------|
| **Registres Modbus** | 45 uniques (59 mots) | DOCUMENTATION_COMMUNICATIONS.md §1 |
| **PGN Victron CAN** | 19 messages | DOCUMENTATION_COMMUNICATIONS.md §2 |
| **Endpoints REST API** | 15+ | DOCUMENTATION_COMMUNICATIONS.md §3 |
| **WebSocket endpoints** | 5 | DOCUMENTATION_COMMUNICATIONS.md §3 |
| **Fichiers source** | 30+ | FILES_REFERENCE.md §4 |

---

## Guide de Démarrage Rapide

### Pour comprendre les **registres Modbus** :
```
1. DOCUMENTATION_COMMUNICATIONS.md → Section 1 (Registres Modbus)
2. Voir uart_bms_protocol.c (lignes 1-577)
3. Consulter uart_bms.h pour la structure de données
```

### Pour comprendre les **messages CAN** :
```
1. DOCUMENTATION_COMMUNICATIONS.md → Section 2 (CAN Victron)
2. Voir conversion_table.c (lignes 50-69)
3. Voir les encodeurs (lignes 325-1300)
```

### Pour comprendre les **APIs** :
```
1. DOCUMENTATION_COMMUNICATIONS.md → Section 3 (APIs)
2. Voir web_server.h (lignes 14-35)
3. Voir web_server.c (lignes 2609-2878)
```

---

## Résumé des Données Clés

### Protocole Modbus (UART)
- **Polling interval** : 250 ms
- **Total words** : 59 (59 * 2 bytes = 118 bytes par cycle)
- **Response timeout** : 200 ms
- **Registres uniques** : 45

**Adresses principales :**
- `0x0000-0x000F` : Voltages cellules (16)
- `0x0024-0x0026` : Pack Voltage/Current
- `0x002E` : SOC haute précision
- `0x0131-0x0140` : Configuration batterie

### Protocole CAN Victron
- **Bitrate** : 500 kbps
- **Format** : Standard (11-bit)
- **PGN** : 19 messages différents
- **Keepalive** : 0x305 (1000 ms)

**Messages clés :**
- `0x351` : CVL/CCL/DCL (charge limits)
- `0x355` : SOC/SOH
- `0x378` : Energy Counters

### Web API
- **Base URL** : `http://<device>/api`
- **WebSocket URL** : `ws://<device>/ws/*`
- **Rate limit** : 10 msg/sec par client
- **Endpoints** : 15+ REST, 5 WebSocket

---

## Flux de Données Global

```
┌──────────────────────────────────────────────────────────────┐
│                         TinyBMS Battery                       │
│                    (Modbus RTU sur UART)                     │
└────────────────────────┬─────────────────────────────────────┘
                         │ 250ms polling
                         ├─ 59 words
                         └─ 45 registres uniques
                         ↓
┌──────────────────────────────────────────────────────────────┐
│                     ESP32 Gateway                             │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ uart_bms.cpp (UART parsing)                             │ │
│  │ → uart_bms_live_data_t                                  │ │
│  └────────────────────┬────────────────────────────────────┘ │
│                       │                                       │
│  ┌────────────────────▼────────────────────────────────────┐ │
│  │ conversion_table.c (BMS → CAN conversion)              │ │
│  │ - 19 PGN encoders (0x307-0x382)                        │ │
│  │ - Energy counters (NVS persistence)                     │ │
│  │ → can_publisher_frame_t                                 │ │
│  └────────────────────┬────────────────────────────────────┘ │
│                       │                                       │
│  ┌────────────────────▼────────────────────────────────────┐ │
│  │ can_victron.c (CAN TWAI Driver)                         │ │
│  │ - 250 kbps, GPIO 7/6                                    │ │
│  │ - Keepalive 0x305 (1000ms)                              │ │
│  │ - Thread-safe publish                                    │ │
│  └────────────────────┬────────────────────────────────────┘ │
│                       │                                       │
│  ┌────────────────────▼────────────────────────────────────┐ │
│  │ web_server.c (HTTP/WebSocket)                           │ │
│  │ - 15+ REST endpoints (/api/...)                         │ │
│  │ - 5 WebSocket endpoints (/ws/...)                       │ │
│  │ - JSON serialization                                     │ │
│  │ - Rate limiting (10 msg/sec)                            │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
                         ↓
            ┌────────────────────────────────┐
            │ Cerbo GX / Victron System      │
            │ (19 PGN CAN)                   │
            └────────────────────────────────┘

            ┌────────────────────────────────┐
            │ Web Browser / Client           │
            │ (REST + WebSocket)             │
            └────────────────────────────────┘
```

---

## Fichiers Source Clés par Catégorie

### Modbus/UART BMS
```
/main/uart_bms/uart_bms_protocol.{h,c}     → 45 registres
/main/uart_bms/uart_bms.{h,cpp}            → Structure de données
/main/uart_bms/uart_frame_builder.*        → Construction frames
/main/uart_bms/uart_response_parser.*      → Parsing réponses
```

### CAN Victron
```
/main/can_victron/can_victron.{h,c}        → TWAI driver
/main/can_publisher/conversion_table.{h,c} → PGN definitions + encoders
/main/can_publisher/can_publisher.{h,c}    → Publisher interface
/main/can_publisher/cvl_*.{h,c}            → CVL logic
/main/include/can_config_defaults.h        → Configuration
```

### Web API
```
/main/web_server/web_server.{h,c}          → REST endpoints
/main/web_server/web_server_alerts.*       → Alerts API
/web/src/js/utils/fetchAPI.js              → Client wrapper
/web/src/js/utils/canTooltips.js           → CAN descriptions
```

---

## Comment Modifier le Projet

### Ajouter un nouveau registre Modbus
1. Éditer `uart_bms_protocol.h` : ajouter enum
2. Éditer `uart_bms_protocol.c` : ajouter à table
3. Mettre à jour `UART_BMS_REGISTER_WORD_COUNT`

### Ajouter un nouveau message CAN
1. Éditer `conversion_table.c` : ajouter PGN
2. Implémenter encoder function
3. Ajouter à channel registry

### Ajouter un nouvel endpoint API
1. Éditer `web_server.h` : documenter
2. Éditer `web_server.c` : implémenter + register
3. Tester avec curl ou Postman

### Ajouter un WebSocket event
1. Éditer `web_server.c` : ajouter broadcast call
2. Encoder JSON payload
3. Envoyer via `ws_client_list_broadcast()`

---

## Recherches Utiles

```bash
# Trouver un registre Modbus
grep -n "0x00YY" main/uart_bms/uart_bms_protocol.c

# Trouver un PGN CAN
grep -n "0xPGN" main/can_publisher/conversion_table.c

# Lister tous les endpoints API
grep -n "\.uri = " main/web_server/web_server.c

# Trouver les WebSocket handlers
grep -n "/ws/" main/web_server/web_server.c
```

---

## Contacts et Support

Pour des questions sur :
- **Registres Modbus** : Voir `uart_bms_protocol.{h,c}`
- **Messages CAN** : Voir `conversion_table.c`
- **APIs Web** : Voir `web_server.{h,c}`
- **Structures de données** : Voir `uart_bms.h`

---

**Documentation Version** : 1.0  
**Date** : 2025-11-10  
**Projet** : TinyBMS-GW (ESP32 Victron Gateway)

### Fichiers Associés
- `DOCUMENTATION_COMMUNICATIONS.md` - Documentation complète
- `COMMUNICATION_REFERENCE.json` - Données structurées
- `FILES_REFERENCE.md` - Référence rapide fichiers
- `ANALYSIS_SUMMARY.txt` - Résumé textuel

