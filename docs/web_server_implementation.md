# Web Server Implementation Proposal

This document outlines a concrete implementation plan for the embedded web server
running on the ESP32-CAN-X2. It expands the high-level architecture with module
boundaries, task interactions, and technology choices aligned with the project
requirements.

## Goals and Constraints
- Serve a responsive HTML5 dashboard with multi-tab navigation (battery, UART,
  CAN, history, configuration).
- Refresh live data within 1 second and push event bus notifications with
  <200 ms latency using WebSockets.
- Support configuration reads/writes for ~30 TinyBMS registers via REST.
- Persist a rolling history (~1000 samples/channel) using SPIFFS or LittleFS.
- Operate under ESP-IDF (v5.x) with AsyncWebServer + AsyncTCP while coexisting
  with FreeRTOS tasks for UART, CAN, and monitoring.

## Component Breakdown
### 1. HTTP Server Task (`web_server_task`)
- **Responsibilities**: Mount SPIFFS/LittleFS, bootstrap AsyncWebServer,
  register REST handlers, and serve static assets under `/`.
- **Lifecycle**:
  1. Wait for Wi-Fi connectivity (AP or STA mode via `config_manager`).
  2. Mount filesystem (SPIFFS by default, pluggable for LittleFS).
  3. Load TLS certificates (optional future work) from NVS or filesystem.
  4. Start AsyncWebServer on port 80 (or 443 if TLS enabled).
- **Static asset serving**: Map `/` to `/index.html`, `/static/*` to files within
  the web partition, leverage `AsyncStaticWebHandler` with gzip support when the
  asset includes `.gz` counterpart.

### 2. REST API Layer
- **Endpoints** (matches `docs/api_endpoints.md`):
  - `GET /api/status` → aggregates from `monitoring` module.
  - `GET /api/config` → fetches current configuration snapshot from
    `config_manager`.
  - `POST /api/config` → validates payload against schema (length, range,
    dependencies) before committing updates to NVS and broadcasting an
    `EVENT_CONFIG_UPDATED` on the bus.
  - `POST /api/ota` → streams firmware image into OTA partition with progress
    callbacks; replies when the upload completes and schedules a reboot.
- **Implementation notes**:
  - Use `AsyncJsonResponse` and `DynamicJsonDocument` for payloads.
  - Validate concurrently by delegating to `config_manager` helpers.
  - Impose authentication hook stub (e.g., token header) to secure endpoints.

### 3. WebSocket Gateways
- **Channels**:
  - `/ws/telemetry` → subscribes to `EVENT_TELEMETRY_TICK` from `monitoring` and
    pushes JSON snapshots every second.
  - `/ws/events` → streams `event_bus` notifications (alarms, config changes,
    UART/CAN status) with message filtering per client preferences.
- **Implementation**:
  - Register two `AsyncWebSocket` instances and attach connection event hooks
    to track clients.
  - Each hook registers a lightweight subscription on the event bus delivering
    `event_bus_message_t` to a shared queue processed by the WebSocket task.
  - Use binary frames for compact telemetry or JSON text frames depending on UI
    needs (default JSON for readability).

### 4. Event Bus Integration
- `web_server` module exposes `web_server_publish_event(const event_bus_message_t
  *msg)` to allow other modules to publish messages destined for WebSocket clients.
- The WebSocket dispatcher task bridges bus messages to clients while enforcing
  rate limiting (e.g., drop duplicates within 250 ms window per metric).
- For configuration updates, `web_server` listens for
  `EVENT_CONFIG_CHANGED` to notify clients and refresh UI panels.

### 5. History Storage Service
- **Data model**: Circular buffers per channel (battery voltage, current, SOC,
  temperatures, UART throughput, CAN counts).
- **Storage**:
  - Maintain an in-RAM ring buffer sized for last 1000 samples.
  - Periodically (every 5 minutes or on explicit request) flush to CSV files in
    `/history/` for persistence.
  - Provide `GET /api/history?channel=<id>&from=&to=` to stream the subset as
    CSV or JSON.
- **Implementation hint**: wrap filesystem access in `history_manager` helper to
  avoid blocking the Async loop; offload heavy I/O to a dedicated FreeRTOS task.

### 6. Frontend Asset Strategy
- Adopt a lightweight CSS framework (Tailwind or Bootstrap). Precompile assets
  on the host and place minified bundles in `web/` with optional gzip versions.
- Use ES6 modules (`dashboard.js`) to create tabs:
  - Battery, UART, CAN panels render tables and gauges updated via WebSocket.
  - History tab fetches data on demand and renders Chart.js time-series graphs.
  - Configuration tab fetches schema (`GET /api/config/schema` future endpoint)
    to build forms dynamically, posts updates via REST, and listens for
    WebSocket confirmations.
- Include reconnect logic for WebSocket (exponential backoff) and offline UI
  indicators.

### 7. Security and Reliability Considerations
- Enforce optional HTTP basic token or session cookie once authentication is
  defined.
- Rate-limit configuration writes and OTA uploads.
- Implement watchdog hooks: if WebSocket task stalls, restart Async server.
- Provide health endpoint `/api/healthz` returning system status (future work).

## Implementation Steps
1. **Filesystem & server bootstrap**: mount SPIFFS, initialize AsyncWebServer,
   serve static assets, add base middleware (logging, CORS, auth stub).
2. **REST handlers**: wire status/config/OTA endpoints, connect to
   `config_manager` and `monitoring` services.
3. **Event bus bridge**: instantiate WebSocket handlers, register event bus
   subscribers, implement message serialization helpers.
4. **History storage**: create RAM ring buffer and background flush task,
   expose `/api/history` endpoint.
5. **Frontend integration**: build responsive layout with tabs, WebSocket client
   logic, Chart.js integration, configuration form bindings.
6. **Testing & validation**: create unit tests for serialization helpers,
   integration tests with `idf.py` + scripted WebSocket client, and manual UI
   verification.

## Dependencies
- ESP-IDF Async components: `AsyncTCP`, `ESPAsyncWebServer` (import via
  component manager or submodule).
- `ArduinoJson` or `cJSON` for JSON processing.
- Chart.js bundled with frontend assets for history plots.

## Deliverables
- `main/web_server/web_server.c` implementing server bootstrap, REST routes,
  WebSocket bridges, and history service integration.
- Updated `web/` assets with responsive layout and real-time dashboards.
- Test utilities in `test/` for simulating WebSocket clients and verifying REST
  responses.
