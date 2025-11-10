# Analyse Détaillée des Interactions UART-CAN via le Bus d'Événements
## Projet ESP-IDF TinyBMS-GW

**Date:** November 2025  
**Branche:** claude/audit-uart-can-interactions  
**Portée:** Analyse du flux de données complet entre UART BMS et CAN Victron

---

## 1. ARCHITECTURE GLOBALE

### 1.1 Flux de Données Principal

```
TinyBMS Hardware (UART)
    ↓ (Réception UART)
┌─────────────────────────────────────────────────────────────┐
│ UART BMS Module (uart_bms_protocol.c)                       │
│  - Lecture des registres du BMS (59 registres)             │
│  - Décodage des trames UART reçues                          │
│  - Parsing des données: tension, courant, température, etc  │
│  - Événement: APP_EVENT_ID_BMS_LIVE_DATA (0x1100)         │
└─────────────────────────────────────────────────────────────┘
    ↓ (Callback UART → CAN Publisher)
┌─────────────────────────────────────────────────────────────┐
│ Event Bus (event_bus.c/h)                                   │
│  - Système de publication/souscription                      │
│  - Queue par abonné (16 événements par défaut)             │
│  - Sémaphore d'accès (mutex)                               │
│  - Format: event_bus_event_t {id, payload*, size}          │
└─────────────────────────────────────────────────────────────┘
    ↓ (Réception données BMS)
┌─────────────────────────────────────────────────────────────┐
│ CAN Publisher (can_publisher.c/h)                           │
│  - Conversion données BMS → frames CAN                      │
│  - Buffer circulaire (8 slots max)                          │
│  - Encoding Victron PGN pour chaque canal CAN             │
│  - Mode: périodique ou immédiat                            │
│  - Événement: APP_EVENT_ID_CAN_FRAME_READY (0x1202)       │
└─────────────────────────────────────────────────────────────┘
    ↓ (Frames préparées)
┌─────────────────────────────────────────────────────────────┐
│ CAN Victron (can_victron.c/h)                              │
│  - Driver TWAI ESP32 (CAN bus)                             │
│  - Transmission physique sur le bus                         │
│  - Keepalive management (0x305)                            │
│  - Réception et publication des frames reçus              │
│  - Événements: APP_EVENT_ID_CAN_FRAME_RAW (0x1200)       │
└─────────────────────────────────────────────────────────────┘
    ↓
Victron Devices (GX device, Inverter, MPPT, etc)
```

---

## 2. CONFIGURATION DU BUS D'ÉVÉNEMENTS

### 2.1 Structure Fondamentale

**Fichier:** `/home/user/TinyBMS-GW/main/event_bus/event_bus.h`

```c
typedef struct {
    event_bus_event_id_t id;      // 32-bit event identifier
    const void *payload;           // Pointer to payload data
    size_t payload_size;           // Size in bytes
} event_bus_event_t;
```

**Propriétés Clés:**
- ✓ Pub/Sub asynchrone
- ✓ Queue par abonné (configurable)
- ✓ Non-blocking (timeout=0) → drop si queue pleine
- ✓ Thread-safe (Sémaphore mutex)
- ✓ Payload référencé (pas copié)

### 2.2 Initialisation et Synchronisation

**Fichier:** `/home/user/TinyBMS-GW/main/event_bus/event_bus.c`

```c
static SemaphoreHandle_t s_bus_lock = NULL;           // Mutex protégeant la liste d'abonnés
static portMUX_TYPE s_init_spinlock = ...;            // Spinlock pour init thread-safe
```

**Mécanismes de Synchronisation:**

| Ressource | Mutex | Spinlock | Timeout | Bloc |
|-----------|-------|----------|---------|------|
| Liste d'abonnés | ✓ | - | portMAX_DELAY | xSemaphoreTake |
| Queue d'événement | (FreeRTOS) | - | 0 (non-block) | xQueueSend |
| Initialisation | - | ✓ (portMUX) | - | portENTER/EXIT_CRITICAL |

---

## 3. ÉVÉNEMENTS ÉCHANGÉS UART ↔ CAN

### 3.1 Définition des Événements

**Fichier:** `/home/user/TinyBMS-GW/main/include/app_events.h`

```c
typedef enum {
    // UART → Application
    APP_EVENT_ID_UART_FRAME_RAW      = 0x1101,   // Hexadecimal string
    APP_EVENT_ID_UART_FRAME_DECODED  = 0x1102,   // Parsed data
    APP_EVENT_ID_BMS_LIVE_DATA       = 0x1100,   // uart_bms_live_data_t
    
    // CAN ↔ Application  
    APP_EVENT_ID_CAN_FRAME_RAW       = 0x1200,   // Reçu du CAN
    APP_EVENT_ID_CAN_FRAME_DECODED   = 0x1201,   // Décodé
    APP_EVENT_ID_CAN_FRAME_READY     = 0x1202,   // Prêt à envoyer
} app_event_id_t;
```

### 3.2 Payload des Événements Critiques

#### Event: BMS_LIVE_DATA (UART → CAN Publisher)
```c
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
    // ... (59 registres de données)
    uint16_t cell_voltage_mv[16];
    uint8_t cell_balancing[16];
    uart_bms_register_entry_t registers[59];
} uart_bms_live_data_t;  // ~500 bytes
```

#### Event: CAN_FRAME_READY (CAN Publisher → CAN Victron)
```c
typedef struct {
    uint32_t id;              // CAN ID (29-bit)
    uint8_t dlc;              // Data length (0-8)
    uint8_t data[8];          // Payload
    uint64_t timestamp_ms;    // Source timestamp
} can_publisher_frame_t;
```

---

## 4. FLUX DE DONNÉES: UART → TRAITEMENT → CAN

### 4.1 Phase 1: Réception UART et Décodage

**Fichier:** `/home/user/TinyBMS-GW/main/uart_bms/uart_bms_protocol.c`

```
┌─ Trame UART Reçue (binaire, format propriétaire TinyBMS)
│
├─ uart_bms_decode_frame():
│  ├─ Validation: header, length, CRC
│  ├─ Extraction: 59 registres (16-bit chacun)
│  ├─ Scaling: application des multiplicateurs (ex: tension × 0.1)
│  └─ Structure: uart_bms_live_data_t
│
└─ Données Décodées (prêtes pour conversion CAN)
```

**Points de Validation:**
- Header error (structure invalide)
- Length error (taille incohérente)
- CRC error (intégrité)
- Timeout (réponse attendue non reçue)
- Missing register (données incomparables)

### 4.2 Phase 2: Publication du BMS_LIVE_DATA

**Fichier:** `/home/user/TinyBMS-GW/main/uart_bms/uart_bms.h`

```c
void uart_bms_set_event_publisher(event_bus_publish_fn_t publisher);
esp_err_t uart_bms_register_listener(uart_bms_data_callback_t callback, void *context);
```

**Mécanisme:**
1. `uart_bms_set_event_publisher()` configure la fonction de publication
2. Données BMS sont passées à `can_publisher_on_bms_update()` (callback)
3. CAN Publisher **n'écoute PAS** le bus d'événements directement
4. Utilise plutôt un mécanisme de **callback synchrone** (uart_bms_register_listener)

### 4.3 Phase 3: Conversion BMS → CAN Frames

**Fichier:** `/home/user/TinyBMS-GW/main/can_publisher/can_publisher.c`

```c
void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    // 1. Préparer CVL (Charger/Courant/Déchargeur)
    can_publisher_cvl_prepare(data);  // CVL logic state machine
    
    // 2. Pour chaque canal CAN configuré:
    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];
        
        // 3. Encoder frame (fill_fn = conversion_table_fill_xxx)
        channel->fill_fn(data, &frame);
        
        // 4. Stocker en buffer (s_frame_buffer) avec mutex
        can_publisher_store_frame(registry->buffer, i, &frame);
        
        // 5. Mode immédiat: envoyer tout de suite
        if (!periodic) {
            can_publisher_dispatch_frame(channel, &frame);
        }
    }
}
```

**Mutexes Utilisés:**
- `s_buffer_mutex` (20ms timeout) → Protège s_frame_buffer
- `s_event_mutex` (20ms timeout) → Protège s_event_frames

### 4.4 Phase 4: Dispatch et Publication CAN

```c
static void can_publisher_dispatch_frame(
    const can_publisher_channel_t *channel,
    const can_publisher_frame_t *frame)
{
    // 1. Envoyer via CAN driver
    if (s_frame_publisher != NULL) {
        s_frame_publisher(channel->can_id, frame->data, frame->dlc, ...);
    }
    
    // 2. Publier événement CAN_FRAME_READY
    can_publisher_publish_event(frame);
}
```

**Ordre Critique:**
1. ✓ Transmission physique CAN en premier
2. ✓ Événement après (garantit CAN envoyé avant notification)

---

## 5. GESTIONNAIRES D'ÉVÉNEMENTS ET PRIORITÉS

### 5.1 Abonnés au Bus d'Événements

**Fichier:** `/home/user/TinyBMS-GW/main/app_main.c`

```c
static void configure_event_publishers(event_bus_publish_fn_t publish_hook)
{
    uart_bms_set_event_publisher(publish_hook);      // Produit events
    can_publisher_set_event_publisher(publish_hook);  // Produit events
    can_victron_set_event_publisher(publish_hook);    // Produit events
    pgn_mapper_set_event_publisher(publish_hook);     // Abonné
    web_server_set_event_publisher(publish_hook);     // Abonné
    config_manager_set_event_publisher(publish_hook); // Abonné
    mqtt_client_set_event_publisher(publish_hook);    // Abonné
    wifi_set_event_publisher(publish_hook);           // Abonné
    monitoring_set_event_publisher(publish_hook);     // Abonné
    // ...
}
```

### 5.2 Schéma Producteur-Consommateur

| Module | Événement Produit | Événement Consommé | Type |
|--------|-------------------|-------------------|------|
| UART BMS | BMS_LIVE_DATA (0x1100) | - | Synchrone (callback) |
| CAN Publisher | CAN_FRAME_READY (0x1202) | BMS_LIVE_DATA | Synchrone (callback) |
| CAN Victron | CAN_FRAME_RAW (0x1200) | CAN_FRAME_READY | Synchrone (TX) |
| Web Server | - | UART_FRAME_*, BMS_*, CAN_* | Asynchrone (queue) |
| Monitoring | - | Tous les événements | Asynchrone (queue) |
| MQTT | - | Tous les événements | Asynchrone (queue) |
| Status LED | - | UART_*, CAN_*, BMS_* | Asynchrone (callback) |

### 5.3 Priorités des Tâches

| Task | Priorité | Rôle | Timeout |
|------|----------|------|---------|
| UART ISR handler | (ISR) | Réception hardware | - |
| CAN Victron task | tskIDLE_PRIORITY + 6 | Keepalive + RX | 50ms boucle |
| CAN Publisher task | tskIDLE_PRIORITY + 2 | Scheduling frames | Variable |
| Main app_main | tskIDLE_PRIORITY + 1 | Initialisation | 1s boucle |
| (Other modules) | Default | Event listeners | - |

---

## 6. MÉCANISMES DE SYNCHRONISATION

### 6.1 Vue Globale des Mutexes et Sémaphores

```
┌────────────────────────────────────────────────────────────────┐
│                    EVENT BUS (CŒUR)                            │
│                                                                │
│  SemaphoreHandle_t s_bus_lock (Mutex)                         │
│  └─ Protège: s_subscribers (linked list)                      │
│     └─ Accès: event_bus_take_lock() → publish() → give_lock() │
│                                                                │
│  Timeout: portMAX_DELAY (jamais timeout)                      │
│  Strategy: Blocking acquisition                               │
└────────────────────────────────────────────────────────────────┘
         ↓
┌────────────────────────────────────────────────────────────────┐
│              CAN PUBLISHER (SYNCHRONISATION)                   │
│                                                                │
│  SemaphoreHandle_t s_buffer_mutex (Mutex)                     │
│  └─ Protège: s_frame_buffer (circular 8 slots)               │
│     └─ Accès: can_publisher_store_frame()                     │
│     └─ Timeout: CAN_PUBLISHER_LOCK_TIMEOUT_MS (20ms)         │
│                                                                │
│  SemaphoreHandle_t s_event_mutex (Mutex)                      │
│  └─ Protège: s_event_frames (8 slots for events)             │
│     └─ Accès: can_publisher_publish_event()                   │
│     └─ Timeout: 20ms                                          │
│                                                                │
│  Note: Remplace ancien portMUX_TYPE spinlock                  │
└────────────────────────────────────────────────────────────────┘
         ↓
┌────────────────────────────────────────────────────────────────┐
│              CAN VICTRON (DRIVER TWAI)                         │
│                                                                │
│  SemaphoreHandle_t s_twai_mutex (Mutex)                       │
│  └─ Protège: TWAI hardware registers (CAN controller)         │
│     └─ Accès: can_victron_publish_frame()                     │
│     └─ Timeout: CAN_VICTRON_LOCK_TIMEOUT_MS (20ms)           │
│                                                                │
│  SemaphoreHandle_t s_driver_state_mutex (Mutex)               │
│  └─ Protège: s_driver_started (boolean flag)                 │
│     └─ Accès: can_victron_is_driver_started()                │
│     └─ Timeout: 20ms                                          │
│                                                                │
│  FreeRTOS Queue: TWAI RX/TX (16 messages each)               │
└────────────────────────────────────────────────────────────────┘
```

### 6.2 Files d'Attente (Queues) FreeRTOS

| Queue | Propriétaire | Taille | Timeout Receive | Usage |
|-------|--------------|--------|-----------------|-------|
| Event Bus subscriber.queue | event_bus | Configurable (16 default) | Blocking (arg timeout) | Pub/Sub events |
| TWAI RX | can_victron | 16 messages | 10ms per frame | Incoming CAN frames |
| TWAI TX | can_victron | 16 messages | - | Outgoing CAN frames |
| CAN Publisher buffer | can_publisher | 8 frames (slots) | Via mutex (20ms) | Periodic publishing |

### 6.3 Modèle de Verrous Détaillé

**Pattern Observé:**

```c
// Exemple: can_publisher_store_frame()
if (s_buffer_mutex != NULL) {
    if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out acquiring lock");
        return false;  // ❌ Fail-safe
    }
    // Section critique (< 20ms)
    buffer->slots[index] = *frame;
    buffer->slot_valid[index] = true;
    xSemaphoreGive(s_buffer_mutex);  // Toujours libérer
    return true;
} else {
    // Fallback sans mutex (early init)
    buffer->slots[index] = *frame;
    return true;
}
```

**Stratégies:**
- ✓ Timeout court (20ms) → Prevents deadlock
- ✓ Fallback non-protected → Tolère init incomplète
- ✓ Always Give après Take → Prévient starvation

---

## 7. POINTS DE BLOCAGE POTENTIELS

### 7.1 Blocage #1: Queue Pleine du Bus d'Événements

**Scenario:**
```
Web Server publie événement → Queue pleine (16/16)
Publication retourne FALSE
Événement PERDU
```

**Cause Root:**
- `event_bus_publish()` utilise `xQueueSend(..., 0)` = non-blocking
- Si queue du subscriber est pleine → event drop
- Counter: `subscriber->dropped_events++` (logs à 1, 2, 4, 8, 16... power of 2)

**Impact sur UART/CAN:**
- ❌ **CAN_FRAME_READY** peut être dropé si subscribers lents
- ✓ Logging averti (esp_log warning)
- ✓ CAN frame reste en buffer, non dupd

**Mitigation:**
```c
// Dans app_main.c - aumentar queue_length:
event_bus_subscribe(32, callback, NULL);  // vs. default 16
```

### 7.2 Blocage #2: Mutex Timeout dans CAN Publisher

**Scenario:**
```
CAN Publisher task essaye d'acquérir s_buffer_mutex
20ms timeout écoulé → LOGW "Timed out acquiring lock"
can_publisher_store_frame() retourne FALSE
Frame non stocké, publication échouée
```

**Cause Root:**
- Autre thread détient le verrou > 20ms
- CAN Victron publish_frame() congestionne le TWAI

**Impact:**
- ⚠️ Perte de données périodiquement
- ✓ Pas de crash (failsafe)

**Symptômes:**
- Logs: "Timed out acquiring CAN publisher buffer"
- Frames manquants sur le bus CAN

### 7.3 Blocage #3: Keepalive CAN Bloqué

**Scenario:**
```
can_victron_task() attend RX keepalive (timeout 50ms)
Si keeper n'envoie pas → Task blocke 50ms
Délai cumulé = latence dans publication CAN
```

**Cause Root:**
```c
// Dans can_victron.c - CAN_VICTRON_TASK_DELAY_MS = 50
static void can_victron_task(void *context) {
    while (true) {
        // ... process RX, keepalive
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms minimum entre cycles
    }
}
```

**Impact:**
- ⚠️ Latence min ~50ms entre frames CAN consecutive
- ✓ Acceptable pour applications non-temps-réel

### 7.4 Blocage #4: Race Condition dans CVL State

**Scenario:**
```
Thread 1 (BMS callback): can_publisher_cvl_prepare(data1)
Thread 2 (CAN Publisher task): Lit s_cvl_state partagé
Inconsistency → frame CVL incorrect
```

**Current State:** 
- CVL state est global statique
- Accès non-protégé par mutex
- ❌ Potential race condition

**Mitigation Need:**
```c
// Ajouter mutex si multi-thread CVL updates
static SemaphoreHandle_t s_cvl_mutex = NULL;
```

### 7.5 Blocage #5: Event Bus Lock Contention

**Scenario:**
```
Beaucoup de publishers simultanés essaient de publish()
s_bus_lock contentious
Latency accumulée
```

**Details:**
```c
// Dans event_bus_publish()
if (!event_bus_take_lock()) {  // portMAX_DELAY
    return false;  // Jamais timeout mais attend
}
// Iterate tous les subscribers
while (subscriber != NULL) {
    xQueueSend(subscriber->queue, event, 0);  // Non-blocking
    subscriber = subscriber->next;
}
xSemaphoreGive(s_bus_lock);
```

**Impact:**
- ⚠️ Lock latency = O(N subscribers)
- Current N ≈ 10-12 modules
- Acceptable mais à monitorer

---

## 8. GESTION D'ERREURS ET TIMEOUTS

### 8.1 Stratégie de Gestion d'Erreurs UART

**Fichier:** `/home/user/TinyBMS-GW/main/uart_bms/uart_bms.h`

```c
#define UART_BMS_RESPONSE_TIMEOUT_MS  200U      // Réponse attendue
#define UART_BMS_POLL_INTERVAL_MS     250U      // Délai entre polls
#define UART_BMS_MIN_POLL_INTERVAL_MS 100U      // Min
#define UART_BMS_MAX_POLL_INTERVAL_MS 1000U     // Max
```

**Diagnostics:**
```c
typedef struct {
    uint32_t frames_total;
    uint32_t frames_valid;
    uint32_t header_errors;
    uint32_t length_errors;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t missing_register_errors;
} uart_bms_parser_diagnostics_t;
```

### 8.2 Stratégie de Gestion d'Erreurs CAN

**Fichier:** `/home/user/TinyBMS-GW/main/can_publisher/can_publisher.c`

```c
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS  20U    // Mutex acquire
#define CAN_PUBLISHER_EVENT_TIMEOUT_MS 50U    // Event publish
```

**Erreurs Observées:**
```c
if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    ESP_LOGW(TAG, "Timed out acquiring CAN publisher buffer lock");
    return false;  // Publication échouée
}

if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
    ESP_LOGW(TAG, "Failed to publish CAN frame event for ID 0x%08X", frame->id);
}
```

### 8.3 Stratégie Keepalive CAN Victron

**Fichier:** `/home/user/TinyBMS-GW/main/can_victron/can_victron.c`

```c
#define CAN_VICTRON_KEEPALIVE_ID       0x305U
#define CAN_VICTRON_TASK_DELAY_MS      50U
#define CAN_VICTRON_RX_TIMEOUT_MS      10U
#define CAN_VICTRON_TX_TIMEOUT_MS      50U
```

**Logique:**
```c
static void can_victron_service_keepalive(uint64_t now) {
    // 1. Envoyer keepalive périodiquement
    if ((now - s_last_keepalive_tx_ms) > INTERVAL_MS) {
        can_victron_send_keepalive(now);
    }
    
    // 2. Vérifier réception de keepalive
    if ((now - s_last_keepalive_rx_ms) > TIMEOUT_MS) {
        s_keepalive_ok = false;  // Keeper disconnecté
    }
}
```

**Keepalive Frame:**
```
CAN ID: 0x305
Format: 1 byte counter/heartbeat
Interval: ~1s (à confirmer)
Timeout: configurable
```

### 8.4 Recovery Patterns

**Pattern 1: Retry avec Backoff**
```c
// À implémenter pour UART timeout
if (error == UART_TIMEOUT) {
    retry_count++;
    delay_ms = min_delay * (1 << retry_count);  // Exponential backoff
    uart_bms_write_register(...);
}
```

**Pattern 2: Graceful Degradation**
```c
// CAN Publisher si lock timeout
if (!can_publisher_store_frame(...)) {
    // Fallback: dispatch immédiat (pas de periodic)
    if (!periodic) {
        can_publisher_dispatch_frame(channel, &frame);
    }
    return;  // Sans buffering
}
```

---

## 9. CARTOGRAPHIE DES FICHIERS CLÉS

### 9.1 Cœur du Système

| Fichier | Rôle | Lignes | Deps |
|---------|------|--------|------|
| `/main/event_bus/event_bus.h` | Définition API bus | 142 | FreeRTOS |
| `/main/event_bus/event_bus.c` | Implémentation pub/sub | 222 | Queue, Sémaphore |
| `/main/include/app_events.h` | Énumération IDs | 62 | event_bus.h |

### 9.2 Module UART → BMS

| Fichier | Rôle | Lignes | Deps |
|---------|------|--------|------|
| `/main/uart_bms/uart_bms.h` | API UART BMS | 114 | event_bus.h |
| `/main/uart_bms/uart_bms_protocol.h` | Registres metadata | 148 | - |
| `/main/uart_bms/uart_bms_protocol.c` | Données registres | 577 | - |

### 9.3 Module CAN → Conversion & Publishing

| Fichier | Rôle | Lignes | Deps |
|---------|------|--------|------|
| `/main/can_publisher/can_publisher.h` | API pub frames | 131 | event_bus.h, uart_bms.h |
| `/main/can_publisher/can_publisher.c` | Scheduler frames | 472 | FreeRTOS, mutex |
| `/main/can_publisher/conversion_table.h` | Encodage canaux | - | uart_bms.h |
| `/main/can_publisher/conversion_table.c` | Fill functions | - | - |
| `/main/can_publisher/cvl_controller.c` | CVL state machine | - | conversion_table |
| `/main/can_publisher/cvl_logic.c` | CVL rules | - | - |

### 9.4 Module CAN → Driver Victron

| Fichier | Rôle | Lignes | Deps |
|---------|------|--------|------|
| `/main/can_victron/can_victron.h` | API TWAI driver | 68 | event_bus.h |
| `/main/can_victron/can_victron.c` | Impl keepalive, RX/TX | 150+ | TWAI driver, FreeRTOS |

### 9.5 Orchestration Principale

| Fichier | Rôle | Lignes | Deps |
|---------|------|--------|------|
| `/main/app_main.c` | Init + main loop | 326 | Tous les modules |

### 9.6 Tests & Validation

| Fichier | Rôle | Lignes | Coverage |
|---------|------|--------|----------|
| `/test/test_event_bus.c` | Unit tests bus | 100+ | Subscribe, publish, dispatch |
| `/test/test_can_publisher_integration.c` | Integration tests | 100+ | BMS→CAN pipeline |
| `/test/test_uart_bms.c` | UART decode tests | - | Frame parsing |

---

## 10. POINTS D'ATTENTION IDENTIFIÉS

### 10.1 🔴 CRITIQUE

#### ❌ Issue #1: Perte d'Événements Possible

**Sévérité:** 🔴 Critique  
**Location:** `event_bus_publish()` (event_bus.c:165-195)

**Description:**
```c
bool event_bus_publish(const event_bus_event_t *event, TickType_t timeout) {
    // ...
    if (xQueueSend(subscriber->queue, event, 0) != pdTRUE) {
        success = false;
        subscriber->dropped_events++;  // ← Compteur de pertes
        if ((subscriber->dropped_events & (subscriber->dropped_events - 1U)) == 0U) {
            ESP_LOGW(TAG, "Dropped event 0x%08X ...", event->id);
        }
    }
}
```

**Problem:** Queue non-blocking → events perdus sans retry

**Impact:**
- Web Server peut manquer notifications UART/CAN
- MQTT métrics incomplets
- Monitoring incomplete

**Fix Recommandé:**
```c
// Option 1: Augmenter queue_length
event_bus_subscribe(32, callback, NULL);  // 16→32

// Option 2: Retry avec timeout > 0 si priorité haute
if (priority_high) {
    xQueueSend(subscriber->queue, event, pdMS_TO_TICKS(10));
} else {
    xQueueSend(subscriber->queue, event, 0);
}
```

#### ❌ Issue #2: Race Condition CVL State

**Sévérité:** 🔴 Critique  
**Location:** `cvl_controller.c` (state machine partagée)

**Description:**
```
UART Thread:  can_publisher_cvl_prepare(data1)
                ↓
              Modifie s_cvl_state global
                ↓
CAN Task:     can_publisher_publish_buffer()
                ↓
              Lit s_cvl_state (peut être inconsistent)
                ↓
              Frame CVL incorrect
```

**Problem:** État CVL modifié sans mutex

**Impact:**
- CVL frame (Charger/DCL) peut contenir valeurs malformées
- Inverters reçoivent commandes incorrectes
- Potentiel dommage équipement

**Fix Required:**
```c
// Ajouter protection CVL
static SemaphoreHandle_t s_cvl_mutex = NULL;

void can_publisher_cvl_prepare(const uart_bms_live_data_t *data) {
    xSemaphoreTake(s_cvl_mutex, pdMS_TO_TICKS(10));
    // ... CVL logic ...
    xSemaphoreGive(s_cvl_mutex);
}
```

### 10.2 🟠 HIGH

#### ⚠️ Issue #3: Timeout Mutex CAN Publisher (20ms)

**Sévérité:** 🟠 High  
**Location:** `can_publisher.c:343, 382`

**Description:**
```c
if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    ESP_LOGW(TAG, "Timed out acquiring CAN publisher buffer");
    return false;  // ← Frame perdu
}
```

**Problem:**
- Si TWAI congestionné → timeout 20ms rapide
- Frame CAN perdu
- Avec 8 slots max, perte cumulative possible

**Scenario:**
```
t=0ms   : Frame1 enter, lock acquired
t=10ms  : Frame2 wait (10ms left)
t=15ms  : TWAI congestionné, slow, Frame1 not released
t=20ms  : Timeout, Frame2 dropped
```

**Impact:**
- Lacune dans télémetrie CAN
- Affecte GX Device (Victron Energy monitoring)

**Recommend:**
```c
// Augmenter timeout pour TWAI lent
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS 50U  // 20→50

// Ou ajouter priorité:
xSemaphoreTake(..., CAN_PRIORITY_HIGH ? portMAX_DELAY : 50);
```

#### ⚠️ Issue #4: Pas de Synchronisation Event Bus ↔ Callbacks UART

**Sévérité:** 🟠 High  
**Location:** `app_main.c:41-42`, `uart_bms.h:85`

**Description:**
```
ARCHITECTURE OBSERVÉE:
UART → Callback (Synchrone)
       └─ can_publisher_on_bms_update() appelé directement

PROBLÈME:
- UART thread appelle callback CAN Publisher
- CAN Publisher utilise mutexes avec timeout
- Si timeout → callback échoue sans retry
```

**Current Flow:**
```
UART ISR/thread
  ↓
uart_bms_process_frame()
  ↓
notify listeners (SYNCHRONE)
  ↓
can_publisher_on_bms_update()
  ↓
xSemaphoreTake(s_buffer_mutex, 20ms timeout) ← PEUT ECHOUER
```

**Problem:**
- Aucune file d'attente entre UART et CAN Publisher
- Perte de synchronisation si CAN Publisher lent

**Recommend:**
```c
// Ajouter queue intermédiaire ou augmenter timeout
can_publisher_on_bms_update() {
    if (xSemaphoreTake(..., 50ms) != pdTRUE) {  // ← Retry
        vTaskDelay(1);
        xSemaphoreTake(..., 50ms);  // Retry une fois
    }
}
```

### 10.3 🟡 MEDIUM

#### ⚠️ Issue #5: Keepalive CAN Peut Bloquer 50ms

**Sévérité:** 🟡 Medium  
**Location:** `can_victron.c:33, 429-441`

**Description:**
```c
static void can_victron_task(void *context) {
    while (true) {
        // ... process RX/keepalive ...
        vTaskDelay(pdMS_TO_TICKS(50));  // ← Minimum 50ms
    }
}
```

**Impact:**
- Latence min = 50ms pour CAN operations
- Peut affecter Keepalive timeout si très resserré

**Scenario:**
```
Keepalive frame timeout = 100ms
Keepalive task cycle = 50ms
Grace period = 50ms ✓ OK
Mais si network congestionné → timeout failure
```

**Recommend:**
```c
// Option 1: Réduire delay
#define CAN_VICTRON_TASK_DELAY_MS 10U  // 50→10

// Option 2: Event-driven plutôt que polling
xQueueReceive(rx_queue, &msg, 50);  // Réactif + timeout
```

#### ⚠️ Issue #6: Payload Event Bus Non Copié

**Sévérité:** 🟡 Medium  
**Location:** `event_bus.h:23-31`

**Description:**
```c
typedef struct {
    const void *payload;       // Référence, pas copie
    size_t payload_size;
} event_bus_event_t;
```

**Problem:**
```
Publisher envoie:
event.payload = &can_publisher_frame_t;
event.payload_size = sizeof(can_publisher_frame_t);

Subscriber récupère après 100ms (slow reader):
const can_publisher_frame_t *frame = (const void*)event.payload;
// ← frame peut être overwritten (circular buffer 8 slots)
```

**Impact:**
- Si subscriber lent → peut lire données ancienne/invalide
- Surtout pour CAN_FRAME_READY (payload stocké en buffer circulaire)

**Mitigation:**
```c
// Actuellement OK car:
// - can_publisher_frame_t stocké en s_event_frames[8]
// - Index circulaire incremental
// - Slot réutilisé après ~8 events seulement
```

**Risk:** ⚠️ Acceptable si subscribers traite event rapidement

### 10.4 🟢 OBSERVATION

#### ✓ Observation #1: Event Bus Bien Conçu pour Async

**Status:** 🟢 Good  
**Location:** `event_bus.c`

**Observation:**
- Pub/Sub pattern simple et efficace
- Queue par abonné = isolation
- Non-blocking par défaut = fail-fast
- Logging de pertes = observable

**Strengths:**
✓ Prévient à un abonné lent de bloquer les autres
✓ Fails fast (no hanging)
✓ Monitoring intégré (dropped_events counter)

#### ✓ Observation #2: CAN Publisher Bien Structuré

**Status:** 🟢 Good  
**Location:** `can_publisher.c`

**Strengths:**
✓ Séparation Frame Buffer (circular) vs Event Frames
✓ Callback synchrone UART → évite queue supplémentaire
✓ Periodic scheduling intelligent (deadline tracking)

---

## 11. RECOMMANDATIONS DE REFACTORING

### 11.1 Court Terme (Critique)

**PR #1: Ajouter Mutex CVL State**
```c
// File: can_publisher/cvl_controller.c
static SemaphoreHandle_t s_cvl_state_mutex = NULL;

void can_publisher_cvl_prepare(const uart_bms_live_data_t *data) {
    xSemaphoreTake(s_cvl_state_mutex, pdMS_TO_TICKS(10));
    // ... existing logic ...
    xSemaphoreGive(s_cvl_state_mutex);
}

void can_publisher_cvl_get_state(cvl_state_t *out) {
    xSemaphoreTake(s_cvl_state_mutex, pdMS_TO_TICKS(10));
    *out = s_cvl_state;
    xSemaphoreGive(s_cvl_state_mutex);
}
```

**Effort:** 2-3 hours  
**Risk:** Low (isolated change)  
**Impact:** Élimine race condition CVL

**PR #2: Augmenter Timeouts CAN Publisher**
```c
#define CAN_PUBLISHER_LOCK_TIMEOUT_MS 50U  // 20→50
```

**Effort:** < 1 hour  
**Risk:** Minimal (increase only)  
**Impact:** Réduire perte frames CAN

### 11.2 Moyen Terme

**PR #3: Mécanisme Queue Intermédiaire UART→CAN**
```c
// Ajouter queue entre UART et CAN Publisher pour découpler
// Permet UART de continuer même si CAN Publisher occupé
typedef struct {
    uart_bms_live_data_t data;
    uint64_t timestamp_ms;
} uart_to_can_event_t;

static QueueHandle_t s_uart_to_can_queue = NULL;

// UART thread:
xQueueSend(s_uart_to_can_queue, &event, pdMS_TO_TICKS(10));

// CAN Publisher task:
while (xQueueReceive(s_uart_to_can_queue, &event, pdMS_TO_TICKS(100))) {
    can_publisher_on_bms_update(&event.data, ...);
}
```

**Effort:** 4-6 hours  
**Risk:** Medium (refactor critical path)  
**Impact:** Meilleure résilience UART↔CAN

**PR #4: Event Bus Dynamic Queue Sizing**
```c
// config_manager permet configurer queue_length par subscriber
app_event_id_t event_id;
uint16_t queue_length;  // Flexible per event

void event_bus_subscribe_with_size(event_id, queue_length, callback);
```

**Effort:** 6-8 hours  
**Risk:** Medium (modifie API event_bus)  
**Impact:** Prévient perte événements pour modules critiques

### 11.3 Long Terme

**Design Review: Actor Model**
```
Consider ROS2 or similar for multi-module async coordination
- Décentralisé (vs centralisé event bus)
- Chaque module = entité autonome
- Communication = queues typées
```

**PR #5: Observabilité Améliorée**
```c
typedef struct {
    uint32_t total_drops;
    uint32_t current_queue_size;
    TickType_t last_drop_time;
    uint32_t max_processing_time_ms;
} event_bus_stats_t;

event_bus_stats_t event_bus_get_subscriber_stats(handle);
```

---

## 12. SOMMAIRE EXÉCUTIF

### Flux UART → CAN
1. ✓ Réception UART (TinyBMS hardware)
2. ✓ Décodage frame (uart_bms_protocol.c)
3. ✓ Callback synchrone → can_publisher_on_bms_update()
4. ✓ Conversion registres BMS → frames CAN (conversion_table.c)
5. ✓ Buffer circulaire 8 slots (s_frame_buffer)
6. ✓ Transmission TWAI (can_victron_publish_frame)
7. ✓ Publication événement CAN_FRAME_READY sur bus
8. → Observateurs (Web, MQTT, Monitoring) reçoivent notification

### Points Critiques
- 🔴 Race condition CVL state (MUST FIX)
- 🔴 Event dropping possible si queue pleine
- 🟠 Mutex timeout 20ms peut perdre frames CAN
- 🟠 Pas de découplage UART↔CAN Publisher

### Synchronisation
- ✓ Event Bus: Sémaphore + Queue par subscriber
- ✓ CAN Publisher: 2 mutexes (buffer + events)
- ✓ CAN Victron: Mutex TWAI + driver state
- ⚠️ CVL State: ❌ NON PROTÉGÉ

### Timeouts Critiques
- Event Bus: portMAX_DELAY (jamais timeout)
- CAN Publisher lock: 20ms (⚠️ peut échouer)
- CAN Victron lock: 20ms
- Keepalive task: 50ms boucle

### Recommandation Immédiate
1. **URGENT:** Ajouter mutex CVL state machine
2. **HIGH:** Augmenter timeout CAN Publisher à 50ms
3. **HIGH:** Ajouter queue découplage UART↔CAN
4. **MEDIUM:** Monitorer event drops en production

---

## Annexe A: Structure de Données Clé

### uart_bms_live_data_t (UART → CAN)
- 500+ bytes
- 59 registres
- 16 tensions cellule
- Timestamp 64-bit
- État système (balancing, alarmes, warnings)

### can_publisher_frame_t (CAN → Bus)
- 12 bytes
- CAN ID 11/29-bit
- Payload 0-8 bytes
- Timestamp 64-bit

### event_bus_event_t (Bus → Subscribers)
- 16 bytes (ID, payload ptr, size)
- Payload référencé, pas copié
- Timeout configurable

---

**Fin de l'Analyse**

