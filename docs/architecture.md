# TinyBMS Web Gateway Architecture

This document describes the high-level architecture of the TinyBMS Web Gateway firmware, covering task breakdown, communication flows, and storage layout.

## Overview
The gateway is split into four layers tightly coupled to the ESP-IDF execution model:

1. **Acquisition drivers** collect TinyBMS data over UART (`uart_bms`) and optional Victron frames for diagnostics (`can_victron`).
2. **Service layer** normalises the data (`pgn_mapper`, `can_publisher`, `monitoring`) and applies configuration/state machines.
3. **Connectivity** exposes telemetry/control interfaces (`web_server`, `mqtt_client`, `wifi`).
4. **Infrastructure** provides shared services (`event_bus`, `config_manager`, persistent storage, logging helpers).

The layers run as FreeRTOS tasks pinned to core 0 by default. Each task publishes or consumes events using the event bus to avoid direct coupling.【F:main/event_bus/event_bus.h†L1-L120】

## Task Breakdown
| Task | Stack (bytes) | Priority | Role |
| ---- | ------------- | -------- | ---- |
| `uart_bms_task` | 4096 | `tskIDLE_PRIORITY+4` | Poll TinyBMS registers, decode frames into `uart_bms_live_data_t`. |
| `can_victron_task` | 4096 | `tskIDLE_PRIORITY+6` | Manage the TWAI driver, keepalive 0x305, TX PGN queue, RX watchdog.【F:main/can_victron/can_victron.c†L1-L210】 |
| `can_publisher_task` | 4096 | `tskIDLE_PRIORITY+5` | Build payloads using `conversion_table.c` and publish `event_bus` messages every `CONFIG_TINYBMS_CAN_PUBLISHER_PERIOD_MS`.【F:main/can_publisher/can_publisher.c†L27-L332】 |
| `monitoring_task` | 4096 | `tskIDLE_PRIORITY+3` | Aggregate metrics, compute alarms, persist diagnostics for the UI. |
| `web_server_task` | 6144 | `tskIDLE_PRIORITY+2` | Host HTTP/WebSocket endpoints and stream state changes. |
| `mqtt_client_task` | 4096 | `tskIDLE_PRIORITY+2` | Maintain MQTT session, forward selected PGNs/alarms. |

All tasks subscribe to relevant event IDs to decouple producers from consumers. For example, `uart_bms` publishes `APP_EVENT_BMS_UPDATE`, consumed by `pgn_mapper` and `monitoring` concurrently.【F:main/pgn_mapper/pgn_mapper.c†L1-L41】

## Data Flow
1. `uart_bms` polls TinyBMS frames (1 Hz par défaut) et publie les données normalisées sur l'`event_bus`.
2. `pgn_mapper` alimente `can_publisher`, qui assemble les payloads PGN (0x351, 0x355, 0x356, 0x35A, etc.) et les place dans la file CAN.
3. `can_victron` sérialise les PGN sur le bus TWAI, gère les keepalive 0x305 et surveille les timeouts `CONFIG_TINYBMS_CAN_KEEPALIVE_TIMEOUT_MS`.
4. `monitoring` calcule les statistiques (énergie cumulée, deltas de cellules) et expose des snapshots via `web_server` et `mqtt_client`.
5. `config_manager` synchronise les paramètres OTA/NVS (Wi-Fi, limites courants, identifiants Victron) et notifie les modules concernés.

## Storage Layout
- **Flash partitions** : `partitions.csv` définit NVS (config), deux slots OTA, SPIFFS pour l'UI et la capture logs.
- **Configuration** : `sdkconfig.defaults` fixe les valeurs de base (GPIO CAN, Wi-Fi, identifiants Victron) et `main/include/app_config.h` complète les constantes.
- **Web assets** : fichiers du dossier `web/` empaquetés dans la partition SPIFFS pendant `idf.py build`.
- **Tests** : scénarios Unity/Catch2 dans `test/` activés via `idf.py test` et pipeline CI (voir `docs/operations.md`).

## PGN & Conversion Responsibilities
- `main/can_publisher/conversion_table.c` centralises scaling factors, clamping rules et mapping TinyBMS → Victron.【F:main/can_publisher/conversion_table.c†L16-L702】
- `docs/pgn_conversions.md` documente les formules et seuils d'alarmes pour chaque PGN.
- `test/test_can_conversion.c` vérifie les conversions nominales/extrêmes pour tous les PGN gérés.【F:test/test_can_conversion.c†L1-L340】

## Operational Notes
- `event_bus` est thread-safe et tolère la saturation via un tampon circulaire de 8 événements ; ajuster `CONFIG_TINYBMS_EVENT_BUS_QUEUE_LEN` si les tâches commencent à perdre des messages lors d'essais intensifs.
- `can_victron` arrête automatiquement le driver TWAI en cas de timeout keepalive et tente une relance toutes les `CONFIG_TINYBMS_CAN_KEEPALIVE_RETRY_MS` millisecondes.【F:main/can_victron/can_victron.c†L273-L563】
- `wifi` bascule en mode AP de secours (`CONFIG_TINYBMS_WIFI_AP_FALLBACK`) après `CONFIG_TINYBMS_WIFI_STA_MAX_RETRY` échecs de connexion.【F:main/wifi/wifi.c†L22-L370】
