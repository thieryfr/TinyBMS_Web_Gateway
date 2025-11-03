# Web UI Data Sources Overview

## Tab-by-tab summary

| Tab | Main data displayed | Acquisition method | REST endpoints | WebSocket channels |
| --- | --- | --- | --- | --- |
| **Temps réel – Batterie** | Pack metrics (voltage, current, SOC/SOH), per-cell voltages, balancing bits, alarms, uptime, cycle counter, TinyBMS registers snapshot. | Initial fetch, then streaming updates. | `GET /api/status` (initial snapshot) | `/ws/telemetry` for telemetry stream, `/ws/events` for register/event updates |
| **Temps réel – UART** | Raw and decoded TinyBMS UART frames streamed to the UI. | Live stream only. | – | `/ws/uart` |
| **Temps réel – CAN** | Raw CAN frames and Victron decoding rendered as they arrive. | Live stream only. | – | `/ws/can` |
| **Historique** | Time series samples (timestamp, voltage, current, SOC, temperature) from RAM history and flash archives. | Batched fetch with optional live enrichment. | `GET /api/history`, `GET /api/history/files`, `GET /api/history/archive` | `/ws/telemetry` contributes new live samples |
| **Configuration** | Device settings (name, UART, Wi-Fi, CAN, etc.) and editable TinyBMS register cards. | On-demand fetch and write-back. | `GET /api/config`, `POST /api/config`, `GET /api/registers`, `POST /api/registers` | `/ws/events` confirms register changes |
| **MQTT** | MQTT client configuration and status indicators (connection state, reconnect counters, last error/event). | Polling while tab is active. | `GET /api/mqtt/config`, `POST /api/mqtt/config`, `GET /api/mqtt/status` | – |

## Why acquisition methods differ

- **Real-time telemetry vs. configuration workflows.** Telemetry data must arrive with minimal latency, so dedicated WebSocket feeds push updates continuously. Configuration and history endpoints exchange heavier payloads that are better suited to explicit REST calls with validation and error handling.
- **Avoiding inefficient polling.** Converting live telemetry to a REST poll would require aggressive refresh intervals, creating redundant traffic and stale data. Conversely, pushing configuration/state changes through a telemetry socket would blur responsibilities and complicate validation.
- **Safety and robustness.** Separating paths lets the firmware enforce permission, persistence, and retry logic appropriate to each domain, while the front end keeps a clean boundary between read-mostly streams and write-capable forms.
