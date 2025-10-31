# TinyBMS Web Gateway Architecture

This document describes the high-level architecture of the TinyBMS Web Gateway firmware, covering task breakdown, communication flows, and storage layout.

## Overview
- **Event-driven core** managed via a lightweight event bus.
- **Drivers** for TinyBMS UART and Victron CAN.
- **Service layer** responsible for PGN mapping, configuration management, and monitoring.
- **Connectivity layer** exposing web UI and MQTT telemetry.

## Module Responsibilities
| Module | Description |
| ------ | ----------- |
| `event_bus` | Provides pub/sub style message routing between tasks. |
| `uart_bms` | Interfaces with the TinyBMS over UART and publishes decoded data. |
| `can_victron` | Transmits and receives Victron-compliant CAN PGNs. |
| `pgn_mapper` | Translates TinyBMS structures into CAN PGNs and vice-versa. |
| `web_server` | Serves the web dashboard and REST/WebSocket APIs. |
| `config_manager` | Persists configuration into NVS and exposes runtime accessors. |
| `mqtt_client` | Handles external telemetry publication and remote control. |
| `monitoring` | Aggregates metrics, logs, and diagnostics for the UI. |

## Data Flow
1. `uart_bms` polls TinyBMS frames and publishes structured events.
2. `pgn_mapper` converts them into Victron PGNs consumed by `can_victron`.
3. `monitoring` aggregates values for the UI and MQTT topics.
4. `web_server` streams updates to the dashboard while `config_manager` persists user changes.

## Storage Layout
- **Flash partitions** defined in `partitions.csv` (NVS, OTA slots, SPIFFS).
- **Web assets** stored in `web/` and embedded into SPIFFS during build.
- **Configuration defaults** tracked in `sdkconfig.defaults` and `app_config.h`.
