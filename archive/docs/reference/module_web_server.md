# Module `web_server`

## Références
- `main/web_server/web_server.h`
- `main/web_server/web_server.c`
- `main/include/app_events.h`
- `main/config_manager/config_manager.h`
- `main/monitoring/monitoring.h`
- `main/mqtt_gateway/mqtt_gateway.h`

## Diagramme UML
```mermaid
graph TD
    subgraph HTTP Server (ESP-IDF httpd)
        HTTPD -->|handlers| API[Handlers REST]
        HTTPD -->|WS| WS[Gestion WebSocket]
    end
    API --> CFG[config_manager]
    API --> MON[monitoring]
    API --> MQTT[mqtt_gateway]
    WS -->|broadcast| Clients
    WebServer --> EventBus
    EventBus --> WebServer
```

## Rôle et responsabilités
Le module `web_server` expose l'interface HTTP/WS de l'application :
- sert les fichiers statiques depuis SPIFFS (`/spiffs`), incluant le frontend web (`index.html`);
- fournit les endpoints REST `/api/status`, `/api/config`, `/api/ota`, `/api/registers`, etc.;
- ouvre plusieurs WebSockets (`/ws/telemetry`, `/ws/events`, `/ws/uart`, `/ws/can`) pour diffuser en temps réel les évènements du système.

## Initialisation
1. `web_server_set_event_publisher()` reçoit la fonction de publication (pour notifier via `APP_EVENT_ID_UI_NOTIFICATION`, etc.).
2. `web_server_init()` :
   - Monte SPIFFS (`esp_vfs_spiffs_register`).
   - Configure et démarre le serveur `httpd_handle_t`.
   - Enregistre les handlers HTTP (GET/POST) et WebSocket.
   - Crée un abonnement `event_bus_subscribe()` pour recevoir les évènements applicatifs pertinents.
   - Démarre une tâche dédiée (`s_event_task_handle`) qui lit la file d'évènements et déclenche les broadcasts WS.

## Endpoints REST principaux
- `GET /api/status` : récupère `monitoring_get_status_json()` et y adjoint l'état MQTT/Wi-Fi.
- `GET /api/history` : `monitoring_get_history_json()` (limite via `limit=` dans la query).
- `GET /api/config` : appelle `config_manager_get_config_json()`.
- `POST /api/config` : lit un corps JSON, invoque `config_manager_set_config_json()` et publie une notification.
- `GET /api/registers` & `POST /api/registers` : interfaces de lecture/écriture des registres TinyBMS via `config_manager` et `uart_bms`.
- `POST /api/ota` : réception d'un firmware et publication de `APP_EVENT_ID_OTA_UPLOAD_READY`.
- `GET /api/mqtt` : exposition de l'état courant de `mqtt_gateway_get_status()`.

## WebSockets
- **/ws/telemetry** : diffuse les payloads `APP_EVENT_ID_TELEMETRY_SAMPLE`.
- **/ws/events** : flux d'évènements UI (`APP_EVENT_ID_UI_NOTIFICATION`, `APP_EVENT_ID_CONFIG_UPDATED`).
- **/ws/uart** : envoie les trames TinyBMS brutes/décodées (`APP_EVENT_ID_UART_FRAME_*`).
- **/ws/can** : diffuse les trames CAN (`APP_EVENT_ID_CAN_FRAME_*`).

Chaque websocket maintient une liste chaînée de clients (`ws_client_t`) protégée par `s_ws_mutex`. Les émissions asynchrones utilisent `httpd_ws_send_frame_async()` afin de ne pas bloquer le thread httpd.

## Gestion des évènements
- `s_event_subscription` lit depuis le bus et, selon l'ID, relaie vers les WebSockets appropriées.
- Certaines notifications sont transformées (ex. injection de timestamps via `web_server_publish_ui_notification`).
- Les payloads volumineux (history, MQTT status) sont préformatés dans des buffers de taille fixe (`WEB_SERVER_HISTORY_JSON_SIZE`, `WEB_SERVER_MQTT_JSON_SIZE`).

## Sécurité & limitations
- Les uploads OTA sont limités par `WEB_SERVER_FILE_BUFSZ` et la taille des partitions OTA (vérification côté handler).
- Les contenus statiques sont servis en lecture seule; un fallback 404 gère les chemins inconnus.
- Les erreurs d'écriture SPIFFS/OTA renvoient des codes HTTP appropriés (`413`, `500`, etc.).

## Extensibilité
- Pour ajouter un nouvel endpoint REST : déclarer un `httpd_uri_t` et l'enregistrer dans `web_server_init()`.
- Pour diffuser un nouvel évènement sur WebSocket, étendre le switch dans la tâche d'évènements et ajouter la liste de clients correspondante.
- Adapter les buffers statiques si la taille des messages augmente.
