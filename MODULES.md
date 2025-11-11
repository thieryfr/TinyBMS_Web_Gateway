# Référence des modules TinyBMS-GW

Ce document détaille tous les modules du firmware TinyBMS-GW, leurs APIs et leurs interactions.

---

## 📋 Table des matières

1. [uart_bms](#uart_bms) - Communication TinyBMS
2. [can_victron](#can_victron) - Driver CAN Victron
3. [can_publisher](#can_publisher) - Publication CAN
4. [event_bus](#event_bus) - Bus événements pub/sub
5. [web_server](#web_server) - Serveur HTTP/WebSocket
6. [mqtt_client](#mqtt_client) - Client MQTT/MQTTS
7. [mqtt_gateway](#mqtt_gateway) - Gateway MQTT
8. [config_manager](#config_manager) - Gestion configuration
9. [alert_manager](#alert_manager) - Système d'alertes
10. [history_logger](#history_logger) - Logging historique
11. [monitoring](#monitoring) - Métriques système
12. [ota_update](#ota_update) - Mises à jour OTA
13. [wifi](#wifi) - Gestion WiFi

---

## 1. uart_bms

**Fichiers** : `main/uart_bms/uart_bms.cpp`, `uart_bms.h`

**Rôle** : Communication bidirectionnelle avec le BMS TinyBMS via UART

### Architecture

```
┌────────────────────────────────────┐
│      uart_event_task()             │  Priority: 12
│  (Interrupt-driven avec queue)     │  Stack: 4096
└────────────────┬───────────────────┘
                 │ UART interrupts
                 ▼
┌────────────────────────────────────┐
│   uart_bms_consume_bytes()         │
│   - Accumule données ring buffer   │
│   - Détecte sync bytes (0x7E)      │
└────────────────┬───────────────────┘
                 │ Complete frame
                 ▼
┌────────────────────────────────────┐
│   ResponseParser::decode()         │
│   - Vérifie checksum CRC16         │
│   - Parse champs (voltage, temp...)│
└────────────────┬───────────────────┘
                 │ TinyBMS_LiveData
                 ▼
┌────────────────────────────────────┐
│   uart_bms_notify_listeners()      │
│   - Event bus publish              │
│   - WebSocket broadcast            │
└────────────────────────────────────┘
```

### API publique

```c
/**
 * @brief Initialiser module UART BMS
 *
 * Configure UART1 à 115200 baud, crée task event-driven,
 * initialise mutexes et buffers.
 */
void uart_bms_init(void);

/**
 * @brief Désinitialiser module
 *
 * Arrête task, libère mutexes, ferme driver UART.
 */
void uart_bms_deinit(void);

/**
 * @brief Configurer publisher event bus
 *
 * @param publisher Fonction callback pour publier événements
 */
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);

/**
 * @brief Configurer intervalle de polling
 *
 * @param interval_ms Intervalle en millisecondes (100-10000)
 */
void uart_bms_set_poll_interval_ms(uint32_t interval_ms);

/**
 * @brief Obtenir dernières données BMS (thread-safe)
 *
 * @return Pointeur vers snapshot (lecture seule, volatile)
 * @warning Ne pas stocker le pointeur, copier les données
 */
const TinyBMS_LiveData *uart_bms_get_latest_shared(void);

/**
 * @brief Enregistrer callback sur données BMS
 *
 * @param callback Fonction appelée à chaque nouvelle donnée
 * @param context Contexte utilisateur passé au callback
 * @return ESP_OK ou ESP_ERR_NO_MEM si slots pleins
 */
esp_err_t uart_bms_register_shared_listener(
    uart_bms_shared_callback_t callback,
    void *context
);
```

### Structure de données

```cpp
typedef struct {
    float battery_voltage;           // Volts
    float battery_current;           // Ampères (+ = charge)
    float state_of_charge;           // Pourcentage (0-100)
    float max_cell_voltage;          // Volts
    float min_cell_voltage;          // Volts
    float max_cell_temp;             // Celsius
    float min_cell_temp;             // Celsius
    uint16_t cell_count;
    uint16_t temp_sensor_count;
    uint32_t timestamp_ms;           // Millisecondes depuis boot
    bool data_valid;
} TinyBMS_LiveData;
```

### Configuration

```c
// uart_bms.cpp:35-49
#define CONFIG_TINYBMS_UART_TX_GPIO 37          // Pin TX
#define CONFIG_TINYBMS_UART_RX_GPIO 36          // Pin RX
#define CONFIG_TINYBMS_UART_EVENT_DRIVEN 1      // Event-driven (défaut)
#define UART_BMS_BAUD_RATE 115200
#define UART_BMS_EVENT_QUEUE_SIZE 20
```

### Événements publiés

- `EVENT_UART_BMS_DATA_UPDATE` : Nouvelles données BMS disponibles

### Dépendances

- ESP-IDF `driver/uart.h`
- `event_bus`
- `conversion_table` (pour callbacks)

---

## 2. can_victron

**Fichiers** : `main/can_victron/can_victron.c`, `can_victron.h`

**Rôle** : Driver CAN/TWAI pour communication protocole Victron

### API publique

```c
/**
 * @brief Initialiser driver CAN
 *
 * Configure TWAI à 500 kbps, pins GPIO, filtres CAN.
 *
 * @return ESP_OK ou erreur ESP-IDF
 */
esp_err_t can_victron_init(void);

/**
 * @brief Démarrer driver CAN
 *
 * Active transmission/réception CAN frames.
 *
 * @return ESP_OK ou erreur
 */
esp_err_t can_victron_start_driver(void);

/**
 * @brief Envoyer frame CAN
 *
 * @param id Identifiant CAN (11-bit standard)
 * @param data Données (max 8 bytes)
 * @param data_len Longueur données (0-8)
 * @return ESP_OK ou ESP_ERR_TIMEOUT si queue pleine
 */
esp_err_t can_victron_send_frame(uint32_t id, const uint8_t *data, uint8_t data_len);

/**
 * @brief Vérifier si driver démarré (thread-safe)
 *
 * @return true si driver actif
 */
bool can_victron_is_driver_started(void);
```

### IDs CAN Victron

| ID | Nom | Contenu | Période |
|----|-----|---------|---------|
| 0x351 | Battery voltage/current/temp | V, I, T | 1000ms |
| 0x355 | SOC/SOH | État charge/santé | 1000ms |
| 0x356 | Battery voltage/current/temp | V, I, T (redondant) | 1000ms |
| 0x35A | Alarm/Warning flags | Bits alertes | 1000ms |
| 0x35E | Manufacturer name | "TinyBMS" | 10000ms |
| 0x35F | Battery parameters | Capacité, cycles | 10000ms |
| 0x370 | Manufacturer name 2 | "TINYBMS" | 10000ms |
| 0x373 | Min/Max cell voltage | mV | 1000ms |
| 0x374 | Min/Max temperature | °C | 1000ms |

### Configuration

```c
// can_victron.c:35-45
#define CONFIG_TINYBMS_CAN_TX_GPIO 5
#define CONFIG_TINYBMS_CAN_RX_GPIO 4
#define CAN_VICTRON_BITRATE 500000  // 500 kbps
#define CAN_TX_QUEUE_SIZE 10
```

### Dépendances

- ESP-IDF `driver/twai.h`
- `monitoring` (métriques)

---

## 3. can_publisher

**Fichiers** : `main/can_publisher/can_publisher.c`, `conversion_table.c`

**Rôle** : Orchestration publication CAN et conversion BMS→Victron

### API publique

```c
/**
 * @brief Initialiser module publication CAN
 *
 * S'abonne à EVENT_UART_BMS_DATA_UPDATE via event bus.
 */
void can_publisher_init(void);

/**
 * @brief Publier toutes les frames Victron
 *
 * Convertit TinyBMS_LiveData en frames CAN Victron et envoie.
 *
 * @param data Données BMS à convertir
 */
void can_publisher_publish_victron_frames(const uart_bms_live_data_t *data);

/**
 * @brief Restaurer compteurs énergie depuis NVS
 *
 * @return ESP_OK ou ESP_ERR_NOT_FOUND si pas de données
 */
esp_err_t can_publisher_conversion_restore_energy_state(void);

/**
 * @brief Sauvegarder compteurs énergie dans NVS
 *
 * @return ESP_OK ou erreur NVS
 */
esp_err_t can_publisher_conversion_save_energy_state(void);
```

### Conversion BMS → CAN

```c
// conversion_table.c:120-150
typedef struct {
    uint16_t bms_register;        // Registre TinyBMS source
    uint8_t can_id;               // CAN ID Victron
    uint8_t start_byte;           // Offset dans frame CAN
    uint8_t num_bytes;            // Taille (1, 2, ou 4 bytes)
    float scaling_factor;         // Facteur multiplication
    float offset;                 // Offset ajouté
} ConversionEntry;

// Exemple: Voltage batterie
{
    .bms_register = REG_PACK_VOLTAGE,
    .can_id = 0x351,
    .start_byte = 0,
    .num_bytes = 2,
    .scaling_factor = 100.0f,     // V → cV (centivolts)
    .offset = 0.0f
}
```

### Dépendances

- `event_bus` (subscriber)
- `can_victron` (transmission)
- `uart_bms` (données source)
- NVS (compteurs énergie)

---

## 4. event_bus

**Fichiers** : `main/event_bus/event_bus.c`, `event_bus.h`

**Rôle** : Bus événements pub/sub central pour découplage modules

### Architecture

```
Publishers                 Event Bus                Subscribers
┌──────────┐              ┌──────────┐             ┌──────────┐
│uart_bms  │──publish()──>│          │──notify()──>│can_pub   │
└──────────┘              │  Queue   │             └──────────┘
┌──────────┐              │ (32 max) │             ┌──────────┐
│config    │──publish()──>│          │──notify()──>│mqtt_gw   │
└──────────┘              │  Mutex   │             └──────────┘
┌──────────┐              │          │             ┌──────────┐
│alerts    │──publish()──>│ Dispatch │──notify()──>│web_srv   │
└──────────┘              └──────────┘             └──────────┘
```

### API publique

```c
/**
 * @brief Initialiser event bus
 *
 * Crée queue, mutexes, task dispatch.
 *
 * @return ESP_OK ou ESP_ERR_NO_MEM
 */
esp_err_t event_bus_init(void);

/**
 * @brief S'abonner à un type d'événement
 *
 * @param event_id ID événement à écouter
 * @param callback Fonction appelée sur événement
 * @return ESP_OK ou ESP_ERR_NO_MEM si trop de subscribers
 */
esp_err_t event_bus_subscribe(
    event_bus_event_id_t event_id,
    event_bus_subscriber_t callback
);

/**
 * @brief Publier événement
 *
 * @param event Événement à publier
 * @param timeout Timeout attente queue disponible
 * @return true si publié, false si timeout/erreur
 */
bool event_bus_publish(const event_bus_event_t *event, TickType_t timeout);

/**
 * @brief Fonction getter pour publishers
 *
 * @return Pointeur fonction event_bus_publish (pour injection)
 */
event_bus_publish_fn_t event_bus_get_publisher(void);
```

### Structure événement

```c
typedef struct {
    event_bus_event_id_t id;      // Type événement (enum)
    void *payload;                 // Données (ou NULL)
    size_t payload_size;           // Taille payload
} event_bus_event_t;
```

### Événements système

```c
// app_events.h:15-35
typedef enum {
    EVENT_UART_BMS_DATA_UPDATE = 0x01,
    EVENT_CONFIG_UPDATED = 0x02,
    EVENT_WIFI_CONNECTED = 0x03,
    EVENT_WIFI_DISCONNECTED = 0x04,
    EVENT_MQTT_CONNECTED = 0x05,
    EVENT_MQTT_DISCONNECTED = 0x06,
    EVENT_ALERT_RAISED = 0x07,
    EVENT_ALERT_CLEARED = 0x08,
    EVENT_CAN_TX_SUCCESS = 0x09,
    EVENT_CAN_TX_FAILED = 0x0A,
    EVENT_OTA_UPDATE_STARTED = 0x0B,
    EVENT_OTA_UPDATE_COMPLETED = 0x0C,
} event_bus_event_id_t;
```

### Configuration

```c
// event_bus.c:25-30
#define EVENT_BUS_QUEUE_SIZE 32            // Max événements en attente
#define EVENT_BUS_MAX_SUBSCRIBERS 32       // Max subscribers par event
#define EVENT_BUS_MUTEX_TIMEOUT_MS 5000    // Timeout mutex (prévention deadlock)
```

### Garanties

- **Thread-safe** : Tous les appels protégés par mutex
- **Non-blocking publishers** : Timeout configurable
- **FIFO ordering** : Événements traités dans l'ordre
- **Isolated callbacks** : Exception dans callback n'affecte pas les autres

---

## 5. web_server

**Fichiers** : `main/web_server/web_server.c`, `web_server_alerts.c`, `auth_rate_limit.c`

**Rôle** : Serveur HTTP/HTTPS/WebSocket pour UI et API REST

### Endpoints REST

| Méthode | Endpoint | Description | Auth | CSRF |
|---------|----------|-------------|------|------|
| GET | `/` | Page d'accueil (SPIFFS) | Non | Non |
| GET | `/api/status` | État système | Non | Non |
| GET | `/api/config` | Configuration complète | Oui | Non |
| POST | `/api/config` | Mise à jour config | Oui | Oui |
| GET | `/api/data` | Données BMS temps réel | Non | Non |
| GET | `/api/history` | Historique données | Non | Non |
| POST | `/api/alerts/clear` | Effacer alertes | Oui | Oui |
| GET | `/api/security/csrf` | Obtenir token CSRF | Oui | Non |
| POST | `/api/ota/upload` | Upload firmware OTA | Oui | Oui |
| POST | `/api/system/restart` | Redémarrer device | Oui | Oui |
| WS | `/ws/alerts` | WebSocket alertes temps réel | Non | Non |

### API publique

```c
/**
 * @brief Initialiser serveur web
 *
 * Configure HTTP/HTTPS, authentification, rate limiting,
 * démarre serveur sur port 80 (HTTP) ou 443 (HTTPS).
 */
void web_server_init(void);

/**
 * @brief Arrêter serveur web
 *
 * Ferme toutes connexions, libère ressources.
 */
void web_server_stop(void);

/**
 * @brief Configurer publisher event bus
 *
 * @param publisher Fonction pour publier événements
 */
void web_server_set_event_publisher(event_bus_publish_fn_t publisher);
```

### Authentification

**HTTP Basic Auth** :
- Username/password stockés dans NVS (hash SHA-256 salted)
- Rate limiting : 5 tentatives max, lockout 60s
- CSRF tokens : Valides 5 minutes, liés à username

**Configuration** :
```bash
idf.py menuconfig
# Component config → TinyBMS-GW → Web Server
#   [*] Enable HTTP Basic authentication
#   HTTP username: admin
#   HTTP password: <votre_password>
```

**Headers requis** :
```http
GET /api/config
Authorization: Basic YWRtaW46cGFzc3dvcmQ=

POST /api/config
Authorization: Basic YWRtaW46cGFzc3dvcmQ=
X-CSRF-Token: a1b2c3d4e5f6...
Content-Type: application/json
```

### WebSocket Alerts

```javascript
// Client JavaScript
const ws = new WebSocket('ws://192.168.1.100/ws/alerts');

ws.onmessage = (event) => {
    const alert = JSON.parse(event.data);
    console.log('Alert:', alert.level, alert.message);
};

// Format message
{
    "timestamp": 1234567890,
    "level": "warning",  // "info", "warning", "error"
    "code": "OVER_VOLTAGE",
    "message": "Cell voltage exceeds maximum (4.25V > 4.20V)"
}
```

### Sécurité

- **HTTPS/TLS** : Optionnel, configuré via `https_config.c`
- **Rate limiting** : 5 tentatives auth par IP, exponential backoff
- **CSRF protection** : Requis sur toutes mutations (POST/PUT/DELETE)
- **Input validation** : JSON schema validation sur toutes APIs
- **Security headers** : CSP, X-Frame-Options, X-Content-Type-Options

### Dépendances

- ESP-IDF `esp_http_server.h`
- `config_manager` (lecture/écriture config)
- `alert_manager` (WebSocket broadcast)
- `history_logger` (API historique)
- `ota_update` (upload firmware)
- mbedtls (SHA-256, Base64, TLS)

---

## 6. mqtt_client

**Fichiers** : `main/mqtt_client/mqtt_client.c`, `mqtts_config.c`

**Rôle** : Client MQTT/MQTTS avec support TLS

### API publique

```c
/**
 * @brief Initialiser client MQTT
 *
 * Crée mutex, prépare client (pas de connexion).
 *
 * @return ESP_OK
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Configurer broker MQTT
 *
 * @param config Configuration broker (URI, credentials, keepalive)
 * @return ESP_OK ou erreur
 */
esp_err_t mqtt_client_configure(const mqtt_client_config_t *config);

/**
 * @brief Démarrer connexion MQTT
 *
 * Connecte au broker de manière asynchrone.
 * EVENT_MQTT_CONNECTED publié sur succès.
 *
 * @return ESP_OK si démarré
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Publier message MQTT
 *
 * @param topic Topic MQTT
 * @param data Données (ou NULL)
 * @param data_len Longueur données
 * @param qos QoS (0 ou 1)
 * @param retain Retain flag
 * @return Message ID ou -1 si erreur
 */
int mqtt_client_publish(const char *topic, const char *data, size_t data_len,
                        int qos, int retain);

/**
 * @brief Tester connexion broker
 *
 * Fonction bloquante pour tests (avec timeout).
 *
 * @param config Configuration à tester
 * @param timeout Timeout en ticks FreeRTOS
 * @param error_message Buffer pour message erreur (optionnel)
 * @param error_size Taille buffer erreur
 * @return ESP_OK si connexion réussie
 */
esp_err_t mqtt_client_test_connection(
    const mqtt_client_config_t *config,
    TickType_t timeout,
    char *error_message,
    size_t error_size
);
```

### Configuration MQTTS

**Vérification serveur uniquement** :
```c
mqtt_client_config_t config = {
    .broker_uri = "mqtts://broker.example.com:8883",
    .username = "user",
    .password = "pass",
    .keepalive_seconds = 120
};

// Certificat CA requis dans main/mqtt_client/certs/mqtt_ca_cert.pem
mqtt_client_configure(&config);
mqtt_client_start();
```

**Authentification mutuelle (mTLS)** :
```c
// Certificats requis:
// - main/mqtt_client/certs/mqtt_ca_cert.pem
// - main/mqtt_client/certs/mqtt_client_cert.pem
// - main/mqtt_client/certs/mqtt_client_key.pem

// Activer dans menuconfig:
// Component config → TinyBMS-GW → MQTT
//   [*] Enable MQTTS
//   [*] Enable client certificate authentication
```

### Événements

- `EVENT_MQTT_CONNECTED` : Connexion établie
- `EVENT_MQTT_DISCONNECTED` : Déconnexion

### Auto-reconnect

- Reconnexion automatique avec exponential backoff
- Délais : 1s, 2s, 4s, 8s, 16s, 32s (max)
- Pas de limite tentatives

### Dépendances

- ESP-IDF `mqtt_client.h`
- `mqtts_config` (certificats TLS)
- `event_bus` (événements connexion)

---

## 7. mqtt_gateway

**Fichiers** : `main/mqtt_gateway/mqtt_gateway.c`

**Rôle** : Gateway pour publier données BMS sur MQTT

### Topics MQTT

| Topic | Données | QoS | Retain | Période |
|-------|---------|-----|--------|---------|
| `tinybms/voltage` | Voltage batterie (V) | 0 | Oui | 1s |
| `tinybms/current` | Courant (A) | 0 | Oui | 1s |
| `tinybms/soc` | État charge (%) | 0 | Oui | 1s |
| `tinybms/temperature/max` | Temp max (°C) | 0 | Oui | 1s |
| `tinybms/temperature/min` | Temp min (°C) | 0 | Oui | 1s |
| `tinybms/cells/voltage/max` | Cell max (V) | 0 | Oui | 1s |
| `tinybms/cells/voltage/min` | Cell min (V) | 0 | Oui | 1s |
| `tinybms/status` | JSON complet | 0 | Oui | 5s |
| `tinybms/alerts` | JSON alertes | 1 | Non | À l'événement |

### Format JSON status

```json
{
    "timestamp": 1234567890,
    "battery": {
        "voltage": 52.4,
        "current": -12.5,
        "power": -655.0,
        "soc": 85.2,
        "soh": 98.5
    },
    "cells": {
        "count": 14,
        "voltage_max": 3.85,
        "voltage_min": 3.82,
        "voltage_delta": 0.03
    },
    "temperature": {
        "sensor_count": 4,
        "max": 28.5,
        "min": 26.2,
        "avg": 27.3
    }
}
```

### API publique

```c
/**
 * @brief Initialiser gateway MQTT
 *
 * S'abonne aux événements BMS et alertes.
 */
void mqtt_gateway_init(void);

/**
 * @brief Activer/désactiver publication
 *
 * @param enabled true pour activer
 */
void mqtt_gateway_set_enabled(bool enabled);
```

### Dépendances

- `mqtt_client` (publication)
- `event_bus` (subscription)
- cJSON (génération JSON)

---

## 8. config_manager

**Fichiers** : `main/config_manager/config_manager.c`

**Rôle** : Gestion centralisée configuration NVS

### API publique

```c
/**
 * @brief Initialiser configuration
 *
 * Charge depuis NVS ou utilise valeurs par défaut.
 *
 * @return ESP_OK
 */
esp_err_t config_manager_init(void);

/**
 * @brief Obtenir configuration complète (thread-safe)
 *
 * @param out_config Buffer pour copie configuration
 * @return ESP_OK
 */
esp_err_t config_manager_get_config(tinybms_config_t *out_config);

/**
 * @brief Mettre à jour configuration depuis JSON
 *
 * Valide et applique configuration, sauvegarde dans NVS.
 *
 * @param json_str Chaîne JSON configuration
 * @return ESP_OK ou erreur validation
 */
esp_err_t config_manager_update_from_json(const char *json_str);

/**
 * @brief Sauvegarder configuration dans NVS
 *
 * @return ESP_OK ou erreur NVS
 */
esp_err_t config_manager_save_to_nvs(void);

/**
 * @brief Restaurer configuration par défaut
 *
 * @return ESP_OK
 */
esp_err_t config_manager_reset_to_defaults(void);
```

### Structure configuration

```c
typedef struct {
    // MQTT
    char mqtt_broker_uri[128];
    char mqtt_username[64];
    char mqtt_password[64];
    uint16_t mqtt_keepalive;
    bool mqtt_enabled;

    // CAN
    bool can_enabled;
    uint16_t can_bitrate;  // kbps

    // WiFi
    char wifi_ssid[32];
    char wifi_password[64];
    char wifi_hostname[32];

    // BMS
    uint16_t uart_poll_interval_ms;
    uint16_t soc_full_percent;
    uint16_t soc_empty_percent;

    // Alerts
    float voltage_max_alert;
    float voltage_min_alert;
    float temp_max_alert;
    float current_max_alert;
} tinybms_config_t;
```

### Validation

Toute mise à jour passe par validation stricte :
- **Ranges** : Valeurs numériques dans limites (ex: SOC 0-100%)
- **Formats** : URLs, IPs, SSIDs valides
- **Longueurs** : Strings dans limites buffers
- **Cohérence** : Contraintes logiques (min < max)

### Événements

- `EVENT_CONFIG_UPDATED` : Configuration modifiée

### Dépendances

- NVS (storage persistant)
- `event_bus` (notification)
- cJSON (parsing/génération)

---

## 9. alert_manager

**Fichiers** : `main/alert_manager/alert_manager.c`

**Rôle** : Système centralisé gestion alertes

### API publique

```c
/**
 * @brief Initialiser gestionnaire alertes
 */
void alert_manager_init(void);

/**
 * @brief Lever alerte
 *
 * @param code Code alerte (ex: "OVER_VOLTAGE")
 * @param level Niveau (info/warning/error)
 * @param message Message descriptif
 */
void alert_manager_raise_alert(
    const char *code,
    alert_level_t level,
    const char *message
);

/**
 * @brief Effacer alerte
 *
 * @param code Code alerte à effacer
 */
void alert_manager_clear_alert(const char *code);

/**
 * @brief Obtenir toutes alertes actives (JSON)
 *
 * @param buffer Buffer sortie
 * @param buffer_size Taille buffer
 * @return ESP_OK ou ESP_ERR_INVALID_SIZE
 */
esp_err_t alert_manager_get_active_alerts_json(char *buffer, size_t buffer_size);
```

### Codes alertes

| Code | Niveau | Description |
|------|--------|-------------|
| `OVER_VOLTAGE` | ERROR | Voltage cellule > max |
| `UNDER_VOLTAGE` | ERROR | Voltage cellule < min |
| `OVER_TEMP` | ERROR | Température > max |
| `OVER_CURRENT` | WARNING | Courant > max |
| `CELL_IMBALANCE` | WARNING | Delta voltage > seuil |
| `BMS_COMM_LOST` | ERROR | Pas de données BMS 30s |
| `MQTT_DISCONNECTED` | WARNING | Connexion MQTT perdue |
| `WIFI_DISCONNECTED` | WARNING | WiFi déconnecté |

### Événements

- `EVENT_ALERT_RAISED` : Nouvelle alerte
- `EVENT_ALERT_CLEARED` : Alerte effacée

### Notifications

- **WebSocket** : Broadcast temps réel
- **MQTT** : Publish sur `tinybms/alerts`
- **Logs** : ESP_LOGW/E selon niveau

### Dépendances

- `event_bus` (publication)
- `web_server_alerts` (WebSocket)
- cJSON (génération JSON)

---

## 10. history_logger

**Fichiers** : `main/history_logger/history_logger.c`, `history_fs.c`

**Rôle** : Enregistrement historique données BMS sur SPIFFS

### Format fichier CSV

```
/spiffs/history/2025-01-17.csv

timestamp,voltage,current,soc,temp_max,temp_min
1705500000,52.4,-12.5,85.2,28.5,26.2
1705500005,52.3,-12.4,85.1,28.6,26.3
...
```

### API publique

```c
/**
 * @brief Initialiser logger historique
 *
 * Monte SPIFFS, crée répertoire history.
 *
 * @return ESP_OK ou erreur SPIFFS
 */
esp_err_t history_logger_init(void);

/**
 * @brief Démarrer logging périodique
 *
 * Crée task qui log toutes les 5 secondes.
 */
void history_logger_start(void);

/**
 * @brief Arrêter logging
 */
void history_logger_stop(void);

/**
 * @brief Obtenir données historique (JSON)
 *
 * @param date Date format "YYYY-MM-DD" ou NULL pour aujourd'hui
 * @param buffer Buffer sortie JSON
 * @param buffer_size Taille buffer
 * @return ESP_OK ou erreur
 */
esp_err_t history_logger_get_day_json(
    const char *date,
    char *buffer,
    size_t buffer_size
);
```

### Rotation automatique

- **Rétention** : 7 jours maximum
- **Stratégie** : Oldest evicted automatiquement
- **Taille fichier** : ~1 KB par jour (1 sample / 5s)

### Dépendances

- ESP-IDF SPIFFS
- `event_bus` (données BMS)

---

## 11. monitoring

**Fichiers** : `main/monitoring/monitoring.c`

**Rôle** : Métriques système (CPU, RAM, tasks)

### Métriques collectées

- **Heap** : Free, minimum free, largest free block
- **CPU** : Usage par task, idle %
- **Tasks** : État, stack high water mark
- **WiFi** : RSSI, reconnexions
- **CAN** : TX success/fail, bus errors
- **MQTT** : Connexions, messages publiés

### API publique

```c
/**
 * @brief Initialiser monitoring
 */
void monitoring_init(void);

/**
 * @brief Obtenir métriques système (JSON)
 *
 * @param buffer Buffer sortie
 * @param buffer_size Taille buffer
 * @return ESP_OK ou ESP_ERR_INVALID_SIZE
 */
esp_err_t monitoring_get_system_metrics_json(char *buffer, size_t buffer_size);

/**
 * @brief Afficher stats tasks (debug)
 */
void monitoring_print_task_stats(void);
```

### Dépendances

- FreeRTOS (task stats)
- ESP-IDF (heap, system)

---

## 12. ota_update

**Fichiers** : `main/ota_update/ota_update.c`, `ota_signature.c`

**Rôle** : Mises à jour OTA sécurisées avec vérification signature

### API publique

```c
/**
 * @brief Initialiser module OTA
 *
 * @return ESP_OK
 */
esp_err_t ota_update_init(void);

/**
 * @brief Démarrer mise à jour OTA depuis URL
 *
 * @param url URL firmware (.bin)
 * @return ESP_OK si démarré
 */
esp_err_t ota_update_start_url(const char *url);

/**
 * @brief Écrire chunk firmware OTA
 *
 * Pour upload HTTP progressif.
 *
 * @param data Chunk données
 * @param data_len Longueur chunk
 * @return ESP_OK ou erreur
 */
esp_err_t ota_update_write_chunk(const uint8_t *data, size_t data_len);

/**
 * @brief Finaliser OTA et vérifier signature
 *
 * @param signature Signature RSA firmware (256/512 bytes)
 * @param signature_len Longueur signature
 * @return ESP_OK si signature valide, ESP_FAIL sinon
 */
esp_err_t ota_update_finalize(const uint8_t *signature, size_t signature_len);
```

### Processus OTA sécurisé

```
1. Upload firmware (.bin)
   ↓
2. Écrire dans partition OTA inactive
   ↓
3. Calculer hash SHA-256 firmware
   ↓
4. Vérifier signature RSA avec clé publique
   ↓ PASS
5. Marquer partition valide
   ↓
6. Redémarrer sur nouveau firmware
   ↓
7. Rollback automatique si boot fail
```

### Signature firmware

**Génération** :
```bash
./scripts/sign_firmware.sh build/tinybms-gw.bin
# Génère tinybms-gw.bin.sig (256 ou 512 bytes)
```

**Upload** :
```bash
curl -X POST -u "admin:pass" \
  -F "firmware=@build/tinybms-gw.bin" \
  -F "signature=@build/tinybms-gw.bin.sig" \
  http://192.168.1.100/api/ota/upload
```

### Configuration

```c
// ota_signature.h:15-20
#ifndef CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED
#define CONFIG_TINYBMS_OTA_SIGNATURE_VERIFY_ENABLED 0  // Défaut: désactivé
#endif

// Activer dans menuconfig ou sdkconfig
```

### Dépendances

- ESP-IDF `esp_ota_ops.h`
- mbedtls (RSA, SHA-256)
- `ota_signature` (vérification)

---

## 13. wifi

**Fichiers** : `main/wifi/wifi.c`

**Rôle** : Gestion connexion WiFi station

### API publique

```c
/**
 * @brief Initialiser WiFi
 *
 * Configure station mode, handlers événements.
 *
 * @return ESP_OK
 */
esp_err_t wifi_init(void);

/**
 * @brief Connecter au WiFi
 *
 * Utilise credentials de config_manager.
 *
 * @return ESP_OK si démarré
 */
esp_err_t wifi_connect(void);

/**
 * @brief Déconnecter WiFi
 *
 * @return ESP_OK
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief Obtenir adresse IP
 *
 * @return IP (0 si pas connecté)
 */
uint32_t wifi_get_ip_address(void);
```

### Auto-reconnect

- Reconnexion automatique si déconnecté
- Exponential backoff: 1s, 2s, 4s, 8s, 16s (max)
- Pas de limite tentatives

### Événements

- `EVENT_WIFI_CONNECTED` : Connexion établie
- `EVENT_WIFI_DISCONNECTED` : Déconnexion

### Dépendances

- ESP-IDF WiFi
- `event_bus` (événements)
- `config_manager` (SSID/password)

---

## 📊 Dépendances inter-modules

```
app_main
   ├── wifi ──────────────> config_manager
   ├── uart_bms ──────────> event_bus
   ├── can_victron
   ├── can_publisher ─────> uart_bms, can_victron, event_bus
   ├── event_bus
   ├── config_manager ────> event_bus
   ├── mqtt_client ───────> event_bus, mqtts_config
   ├── mqtt_gateway ──────> mqtt_client, event_bus
   ├── alert_manager ─────> event_bus
   ├── history_logger ────> event_bus
   ├── monitoring
   ├── ota_update ────────> ota_signature
   └── web_server ────────> config_manager, alert_manager, history_logger,
                            auth_rate_limit, https_config
```

---

**Version** : 1.0 (Phase 3)
**Dernière mise à jour** : 2025-01-17
