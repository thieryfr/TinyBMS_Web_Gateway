# API Endpoints

## REST API
| Method | Path | Description |
| ------ | ---- | ----------- |
| GET | `/api/status` | Returns gateway summary information. |
| GET | `/api/config` | Retrieves persisted configuration. |
| POST | `/api/config` | Updates configuration payload (validated before storage). |
| POST | `/api/ota` | Uploads new firmware image for OTA slot. |

## WebSocket
- `ws://<device-ip>/ws/telemetry` — Streams live TinyBMS metrics.
- `ws://<device-ip>/ws/events` — Publishes event bus notifications.
