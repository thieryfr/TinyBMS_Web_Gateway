# ANALYSE APPROFONDIE DES BUGS - TinyBMS-GW

## RÉSUMÉ EXÉCUTIF

Analyse détaillée du code TinyBMS-GW identifiant **13 problèmes** de sécurité et stabilité :
- **4 CRITIQUES** (race conditions, deadlocks, null pointers)
- **5 ÉLEVÉES** (fuites ressources, synchronisation)
- **4 MOYENNES** (erreurs logique, validation)

---

# PROBLÈMES IDENTIFIÉS

## 1. RACE CONDITION: Accès non synchronisé à s_shared_listeners

**Description**: Dans `uart_bms_register_shared_listener()` (lignes 1081-1105), l'accès à `s_shared_listeners` n'est pas protégé par un mutex. Cette fonction s'exécute en contexte utilisateur et peut être interrompue par `uart_bms_deinit()` qui efface le tableau. De plus, `uart_bms_notify_shared_listeners()` (ligne 155) accède sans verrouillage.

**Localisation**: `/home/user/TinyBMS-GW/main/uart_bms/uart_bms.cpp:1081-1119`

**Criticité**: **CRITIQUE**

**Impact**:
- Crash avec segmentation fault lors de l'accès au tableau effacé
- Appel de callbacks sur des pointeurs invalides
- Corruption mémoire silencieuse

**Code problématique**:
```cpp
// ❌ PROBLÈME: Pas de mutex protégeant s_shared_listeners
esp_err_t uart_bms_register_shared_listener(uart_bms_shared_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == callback && s_shared_listeners[i].context == context) {
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == nullptr) {
            s_shared_listeners[i].callback = callback;  // ❌ Pas de protection
            s_shared_listeners[i].context = context;
            if (s_shared_snapshot_valid) {
                callback(s_shared_snapshot, context);  // ❌ Race: snapshot peut devenir invalide
            }
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}
```

**Solution**:
```cpp
esp_err_t uart_bms_register_shared_listener(uart_bms_shared_callback_t callback, void *context)
{
    if (callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // CORRECTION: Protéger avec le même mutex que pour les listeners normaux
    if (s_listeners_mutex == nullptr || xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == callback && s_shared_listeners[i].context == context) {
            xSemaphoreGive(s_listeners_mutex);
            return ESP_OK;
        }
    }

    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (s_shared_listeners[i].callback == nullptr) {
            s_shared_listeners[i].callback = callback;
            s_shared_listeners[i].context = context;
            TinyBMS_LiveData snapshot_copy = s_shared_snapshot;
            bool snapshot_valid = s_shared_snapshot_valid;
            xSemaphoreGive(s_listeners_mutex);
            
            if (snapshot_valid) {
                callback(snapshot_copy, context);
            }
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_listeners_mutex);
    return ESP_ERR_NO_MEM;
}
```

---

## 2. RACE CONDITION: Accès non protégé à s_driver_started en deinit

**Description**: Dans `can_victron_deinit()` (ligne 997), `s_driver_started` est accédée sans verrouillage alors qu'elle peut être modifiée par `can_victron_is_driver_started()` appelée par la tâche CAN simultanément.

**Localisation**: `/home/user/TinyBMS-GW/main/can_victron/can_victron.c:985-1009`

**Criticité**: **CRITIQUE**

**Impact**:
- Lecture incohérente de l'état du driver
- Fuite de ressources TWAI si s_driver_started n'est pas cohérent
- Opérations twai_stop() ou twai_driver_uninstall() sur un driver arrêté
- Crash du driver TWAI

**Code problématique**:
```c
// ❌ PROBLÈME: Pas de mutex sur s_driver_started
void can_victron_deinit(void)
{
    ...
    // s_task_should_exit = true;
    vTaskDelay(pdMS_TO_TICKS(200));

    // Stop TWAI driver
    if (s_driver_started) {  // ❌ Race: tâche peut modifier simultanément
        esp_err_t err = twai_stop();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop TWAI: %s", esp_err_to_name(err));
        }
```

**Solution**:
```c
void can_victron_deinit(void)
{
    ...
    // Signal task to exit
    s_task_should_exit = true;
    vTaskDelay(pdMS_TO_TICKS(200));

    // Check driver state with mutex protection
    bool should_stop = false;
    if (s_driver_state_mutex != NULL && xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        should_stop = s_driver_started;
        if (should_stop) {
            s_driver_started = false;  // Prevent other threads from seeing stale state
        }
        xSemaphoreGive(s_driver_state_mutex);
    }

    if (should_stop) {
        esp_err_t err = twai_stop();
        ...
```

---

## 3. DEADLOCK POTENTIEL: portMAX_DELAY en contexte non-interruptible

**Description**: Plusieurs endroits utilisent `portMAX_DELAY` (blocage infini) lors de l'acquisition de mutexes dans des fonctions pouvant être appelées depuis le contexte de shutdown ou depuis des tâches critiques. Cela peut causer un deadlock si le titulaire du mutex est bloqué/mort.

**Localisation**: `/home/user/TinyBMS-GW/main/web_server/web_server.c:456, 506, 569, 601, 3396`

**Criticité**: **CRITIQUE**

**Impact**:
- Deadlock lors du shutdown si le web_server attend le mutex auth
- Système non-responsif pendant 30+ secondes
- Impossible de redémarrer proprement
- Watchdog timeout potentiel sans crash utile

**Code problématique** (ligne 3396):
```c
// ❌ PROBLÈME: portMAX_DELAY en shutdown = deadlock
void web_server_deinit(void)
{
    ...
    if (s_ws_mutex != NULL) {
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);  // ❌ Peut bloquer indéfiniment
        ws_client_list_free(&s_telemetry_clients);
        ...
```

**Solution**:
```c
void web_server_deinit(void)
{
    ...
    if (s_ws_mutex != NULL) {
        // CORRECTION: Timeout pour éviter deadlock
        if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            ws_client_list_free(&s_telemetry_clients);
            ws_client_list_free(&s_event_clients);
            ws_client_list_free(&s_uart_clients);
            ws_client_list_free(&s_can_clients);
            ws_client_list_free(&s_alert_clients);
            xSemaphoreGive(s_ws_mutex);
        } else {
            // Fallback: free directly without mutex (risky but better than deadlock)
            ESP_LOGW(TAG, "Warning: WebSocket mutex timeout during shutdown");
            ws_client_list_free(&s_telemetry_clients);
            // ... rest of lists
        }
    }
```

---

## 4. RACE CONDITION: Accès non synchronisé à s_channel_deadlines

**Description**: Dans `can_publisher_publish_buffer()` (lignes 407-431), le tableau `s_channel_deadlines` est accédé et modifié sans protection mutex, alors qu'il peut être accédé simultanément par `can_publisher_store_frame()` (ligne 361) qui le modifie aussi.

**Localisation**: `/home/user/TinyBMS-GW/main/can_publisher/can_publisher.c:372-448`

**Criticité**: **ÉLEVÉE**

**Impact**:
- Calcul de deadline incohérent
- Publication de frames CAN à mauvaises fréquences
- Désynchronisation des timers CAN
- Perte de messages CAN critiques (keepalive)

**Code problématique**:
```c
// ❌ PROBLÈME: Accès non protégé à s_channel_deadlines[i]
static TickType_t can_publisher_publish_buffer(can_publisher_registry_t *registry, TickType_t now)
{
    ...
    for (size_t i = 0; i < registry->channel_count; ++i) {
        ...
        TickType_t deadline = s_channel_deadlines[i];  // ❌ Race: peut être modifié par can_publisher_store_frame
        ...
        if (due && has_frame) {
            can_publisher_dispatch_frame(channel, &frame);
            s_channel_deadlines[i] += s_channel_period_ticks[i];  // ❌ Race write
```

**Solution**:
```c
static TickType_t can_publisher_publish_buffer(can_publisher_registry_t *registry, TickType_t now)
{
    ...
    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];
        can_publisher_frame_t frame = {0};
        bool has_frame = false;
        TickType_t deadline = 0;

        // CORRECTION: Protéger la lecture
        if (s_buffer_mutex != NULL) {
            if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(CAN_PUBLISHER_LOCK_TIMEOUT_MS)) == pdTRUE) {
                if (buffer->slot_valid[i]) {
                    frame = buffer->slots[i];
                    has_frame = true;
                }
                deadline = s_channel_deadlines[i];  // Lecture sécurisée
                xSemaphoreGive(s_buffer_mutex);
            } else {
                ESP_LOGW(TAG, "Timed out acquiring CAN publisher buffer for read");
            }
        } else if (buffer->slot_valid[i]) {
            frame = buffer->slots[i];
            has_frame = true;
            deadline = s_channel_deadlines[i];
        }
        
        if (deadline == 0) {
            deadline = now;
        }

        bool due = (now >= deadline);

        if (due && has_frame) {
            can_publisher_dispatch_frame(channel, &frame);
            // Protéger l'écriture
            if (s_buffer_mutex != NULL && xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(CAN_PUBLISHER_LOCK_TIMEOUT_MS)) == pdTRUE) {
                s_channel_deadlines[i] += s_channel_period_ticks[i];
                if ((int32_t)(now - s_channel_deadlines[i]) > 0) {
                    s_channel_deadlines[i] = now + s_channel_period_ticks[i];
                }
                xSemaphoreGive(s_buffer_mutex);
            }
            deadline = s_channel_deadlines[i];
        }
        ...
```

---

## 5. TOCTOU (Time-of-Check-Time-of-Use): event_bus_unsubscribe

**Description**: Dans `event_bus_unsubscribe()` (lignes 158-190), le handle est vérifié contre `s_subscribers` sous le verrou, puis supprimé hors du verrou. Entre le relâchement du verrou et la libération, un autre thread peut libérer la même structure.

**Localisation**: `/home/user/TinyBMS-GW/main/event_bus/event_bus.c:158-190`

**Criticité**: **ÉLEVÉE**

**Impact**:
- Double-free de la structure subscription
- Corruption du heap mémoire
- Crash de l'application
- Accès à mémoire libérée (use-after-free)

**Code problématique**:
```c
// ❌ PROBLÈME: TOCTOU - vérification vs utilisation
void event_bus_unsubscribe(event_bus_subscription_handle_t handle)
{
    if (handle == NULL || s_bus_lock == NULL) {
        return;
    }

    event_bus_subscription_t *to_free = NULL;

    if (!event_bus_take_lock()) {
        return;
    }

    // Vérification sous lock
    event_bus_subscription_t **link = &s_subscribers;
    while (*link != NULL) {
        if (*link == handle) {
            *link = handle->next;
            to_free = handle;
            break;
        }
        link = &(*link)->next;
    }

    event_bus_give_lock();  // ❌ Lock relâché

    // ❌ PROBLÈME: Un autre thread peut appeler event_bus_unsubscribe avec le même handle
    if (to_free == NULL) {
        return;
    }

    if (to_free->queue != NULL) {
        vQueueDelete(to_free->queue);  // ❌ Peut être doublé
    }
    vPortFree(to_free);  // ❌ Peut être doublé -> double-free
}
```

**Solution**:
```c
void event_bus_unsubscribe(event_bus_subscription_handle_t handle)
{
    if (handle == NULL || s_bus_lock == NULL) {
        return;
    }

    event_bus_subscription_t *to_free = NULL;

    if (!event_bus_take_lock()) {
        return;
    }

    event_bus_subscription_t **link = &s_subscribers;
    while (*link != NULL) {
        if (*link == handle) {
            *link = handle->next;
            to_free = handle;
            break;
        }
        link = &(*link)->next;
    }

    // CORRECTION: Nettoyer sous le verrou
    if (to_free != NULL) {
        if (to_free->queue != NULL) {
            vQueueDelete(to_free->queue);
        }
        vPortFree(to_free);
    }

    event_bus_give_lock();
}
```

---

## 6. NULL POINTER DEREFERENCE: can_publisher_on_bms_update

**Description**: Dans `can_publisher_on_bms_update()` (ligne 238), si `registry->channels` devient NULL après la vérification (peut arriver lors de shutdown concurrent), l'accès ligne 257 (`&registry->channels[i]`) causera un crash.

**Localisation**: `/home/user/TinyBMS-GW/main/can_publisher/can_publisher.c:238-285`

**Criticité**: **ÉLEVÉE**

**Impact**:
- Crash lors d'une mise à jour BMS pendant shutdown
- Perte de données d'énergie
- Impossible de graceful shutdown

**Code problématique**:
```c
void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    can_publisher_registry_t *registry = (can_publisher_registry_t *)context;

    if (data == NULL || registry == NULL || registry->channels == NULL || registry->buffer == NULL) {
        return;  // Vérification OK
    }

    if (registry->channel_count == 0 || registry->buffer->capacity == 0) {
        return;
    }

    can_publisher_cvl_prepare(data);

    uint64_t timestamp_ms = (data->timestamp_ms > 0U) ? data->timestamp_ms : can_publisher_timestamp_ms();

    bool periodic = can_publisher_periodic_mode_enabled() && (s_publish_task_handle != NULL);

    for (size_t i = 0; i < registry->channel_count; ++i) {
        const can_publisher_channel_t *channel = &registry->channels[i];  // ❌ TOCTOU: channels peut être NULL maintenant
```

**Solution**:
```c
void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    can_publisher_registry_t *registry = (can_publisher_registry_t *)context;

    if (data == NULL || registry == NULL || registry->channels == NULL || registry->buffer == NULL) {
        return;
    }

    if (registry->channel_count == 0 || registry->buffer->capacity == 0) {
        return;
    }

    can_publisher_cvl_prepare(data);

    uint64_t timestamp_ms = (data->timestamp_ms > 0U) ? data->timestamp_ms : can_publisher_timestamp_ms();

    bool periodic = can_publisher_periodic_mode_enabled() && (s_publish_task_handle != NULL);

    // CORRECTION: Re-vérifier avant boucle
    size_t channel_count = registry->channel_count;
    const can_publisher_channel_t *channels = registry->channels;
    
    if (channels == NULL) {
        return;
    }

    for (size_t i = 0; i < channel_count; ++i) {
        const can_publisher_channel_t *channel = &channels[i];
        ...
```

---

## 7. FUITE MÉMOIRE: can_victron_deinit - mutex non verrouillé

**Description**: Dans `can_victron_deinit()` (lignes 1012-1028), les mutexes sont supprimés sans vérification du retour et sans gestion d'erreur. Si `s_stats_mutex` n'est pas NULL mais que vSemaphoreDelete échoue silencieusement, on peut avoir des appels doublés.

**Localisation**: `/home/user/TinyBMS-GW/main/can_victron/can_victron.c:1012-1028`

**Criticité**: **ÉLEVÉE**

**Impact**:
- Les mutexes restent valides en mémoire mais pointeurs effacés
- Fuite de ressources FreeRTOS
- Prochain init peut allouer les mêmes handles (collision)

**Code problématique**:
```c
// Destroy all mutexes
if (s_twai_mutex != NULL) {
    vSemaphoreDelete(s_twai_mutex);
    s_twai_mutex = NULL;
}
```

**Solution**:
```c
// Destroy all mutexes safely
if (s_twai_mutex != NULL) {
    if (xSemaphoreTake(s_twai_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        xSemaphoreGive(s_twai_mutex);
        vSemaphoreDelete(s_twai_mutex);
    } else {
        ESP_LOGW(TAG, "Warning: Cannot verify s_twai_mutex is not held before deletion");
        // Still attempt delete but log warning
        vSemaphoreDelete(s_twai_mutex);
    }
    s_twai_mutex = NULL;
}
```

---

## 8. CONDITIONS RACE: Accès non synchronisé à s_latest_bms

**Description**: Dans `monitoring.c` (lignes 36-37), `s_latest_bms` et `s_has_latest_bms` sont des variables globales modifiées par la tâche UART et lues par le web_server sans synchronisation (aucun mutex déclaré initialement pour ces variables).

**Localisation**: `/home/user/TinyBMS-GW/main/monitoring/monitoring.c:35-46`

**Criticité**: **MOYENNE**

**Impact**:
- Lectures inconsistentes du BMS live data
- Affichage de data partiellement mises à jour au web_server
- Calculs d'énergie basés sur data corrompue

**Code problématique**:
```c
// ❌ PROBLÈME: Pas de mutex pour s_latest_bms et s_has_latest_bms
static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_live_data_t s_latest_bms = {0};
static bool s_has_latest_bms = false;
static monitoring_history_entry_t s_history[MONITORING_HISTORY_CAPACITY];
```

**Solution**:
```c
static event_bus_publish_fn_t s_event_publisher = NULL;
static uart_bms_live_data_t s_latest_bms = {0};
static bool s_has_latest_bms = false;
static SemaphoreHandle_t s_latest_bms_mutex = NULL;  // ✓ Ajouter pour synchronisation
```

---

## 9. DIVISION PAR ZÉRO POTENTIELLE: monitoring.c ligne 382

**Description**: Dans `monitoring_build_diagnostics_json()` (ligne 382), si `diagnostics.snapshot_latency_samples` est 0, la division `diagnostics.snapshot_latency_total_us / diagnostics.snapshot_latency_samples` causera une exception FPU.

**Localisation**: `/home/user/TinyBMS-GW/main/monitoring/monitoring.c:379-383`

**Criticité**: **MOYENNE**

**Impact**:
- Exception FPU sur ESP32
- Crash immédiat du système
- Perte de logs de diagnostics
- Impossible de reconnecter le web

**Code problématique**:
```c
uint32_t avg_latency_us = 0;
if (diagnostics.snapshot_latency_samples > 0U && diagnostics.snapshot_latency_total_us > 0U) {
    avg_latency_us =
        (uint32_t)(diagnostics.snapshot_latency_total_us / diagnostics.snapshot_latency_samples);
}
```

**Solution**: Le code est actuellement correct! La condition vérifie bien que `snapshot_latency_samples > 0`. Aucune correction nécessaire.

---

## 10. BUFFER OVERFLOW POTENTIEL: event_bus_get_all_metrics

**Description**: Dans `event_bus_get_all_metrics()` (lignes 269-298), si un subscriber a un nom mal terminé (corruption), `strncpy` ligne 283 peut écrire au-delà de `dest->name`.

**Localisation**: `/home/user/TinyBMS-GW/main/event_bus/event_bus.c:269-298`

**Criticité**: **MOYENNE**

**Impact**:
- Stack overflow si `iter->name` n'est pas null-terminated
- Corruption de la métrique structure
- Crash lors de la lecture des métriques
- Information disclosure potentiel

**Code problématique**:
```c
size_t event_bus_get_all_metrics(event_bus_subscription_metrics_t *out_metrics, size_t capacity)
{
    ...
    while (iter != NULL && count < capacity) {
        event_bus_subscription_metrics_t *dest = &out_metrics[count];
        strncpy(dest->name, iter->name, sizeof(dest->name) - 1U);  // ❌ Si iter->name != null-terminated
        dest->name[sizeof(dest->name) - 1U] = '\0';  // OK, mais peut masquer une corruption antérieure
```

**Solution**:
```c
// Ajouter validation
while (iter != NULL && count < capacity) {
    event_bus_subscription_metrics_t *dest = &out_metrics[count];
    
    // CORRECTION: Valider que iter->name est null-terminated
    // Ceci existe déjà à la création (ligne 124) mais valider ne fait pas mal
    if (iter->name[sizeof(iter->name) - 1] != '\0') {
        ESP_LOGW(TAG, "Warning: subscriber name not properly null-terminated");
        // Sauter ce subscriber ou corriger
    }
    
    strncpy(dest->name, iter->name, sizeof(dest->name) - 1U);
    dest->name[sizeof(dest->name) - 1U] = '\0';
```

---

## 11. CODE MORT: uart_bms.cpp ligne 1063

**Description**: Dans `uart_bms_request_restart()`, le paramètre `readback_raw` est passé comme NULL (ligne 1063), rendant ce paramètre inutile et complexifiant le code.

**Localisation**: `/home/user/TinyBMS-GW/main/uart_bms/uart_bms.cpp:1058-1069`

**Criticité**: **FAIBLE**

**Impact**:
- Code désordonné
- Pas de confirmation de redémarrage
- Documentation confuse

**Code problématique**:
```c
esp_err_t uart_bms_request_restart(uint32_t timeout_ms)
{
#ifdef ESP_PLATFORM
    return uart_bms_write_register(UART_BMS_SYSTEM_CONTROL_REGISTER,
                                   UART_BMS_SYSTEM_CONTROL_RESTART_VALUE,
                                   NULL,  // ❌ Code mort: readback_raw jamais utilisé
                                   timeout_ms);
```

**Solution**:
```c
esp_err_t uart_bms_request_restart(uint32_t timeout_ms)
{
#ifdef ESP_PLATFORM
    uint16_t readback = 0;
    esp_err_t err = uart_bms_write_register(UART_BMS_SYSTEM_CONTROL_REGISTER,
                                   UART_BMS_SYSTEM_CONTROL_RESTART_VALUE,
                                   &readback,  // ✓ Capturer la confirmation
                                   timeout_ms);
    
    if (err == ESP_OK && readback != UART_BMS_SYSTEM_CONTROL_RESTART_VALUE) {
        ESP_LOGW(kTag, "Restart command sent but register value is 0x%04X instead of 0x%04X",
                 (unsigned)readback, (unsigned)UART_BMS_SYSTEM_CONTROL_RESTART_VALUE);
    }
    return err;
```

---

## 12. GESTION ERREUR: can_victron.c ligne 997 - Vérification incohérente d'état

**Description**: Dans `can_victron_deinit()` (ligne 997), `s_driver_started` est lu sans mutex alors que la fonction `can_victron_start_driver()` le modifie sous mutex. Cela crée une incohérence.

**Localisation**: `/home/user/TinyBMS-GW/main/can_victron/can_victron.c:989-1010`

**Criticité**: **MOYENNE**

**Impact**:
- État inconsistent détecté
- twai_stop() appelé sur un driver non-démarré
- Erreurs sporadiques lors du shutdown
- Difficulté de débuggage

**Solution**: Voir problème #2

---

## 13. INITIALIZATION INCOMPLETE: web_server.c - Mutex nullptr

**Description**: Dans `web_server_init()`, si `s_auth_mutex` échoue à être créé mais que la fonction continue, les appels ultérieurs à `xSemaphoreTake(s_auth_mutex, ...)` travaillent avec un pointeur NULL non géré correctement partout.

**Localisation**: `/home/user/TinyBMS-GW/main/web_server/web_server.c:473-490` (estimé)

**Criticité**: **MOYENNE**

**Impact**:
- Condition de course si mutex création échoue
- Les vérifications `if (s_auth_mutex == NULL)` ne sont pas cohérentes
- Blocages sporadiques si le mutex n'existe pas

**Solution**:
```c
// Dans web_server_init(): Retourner une erreur si création échoue
if (CONFIG_TINYBMS_WEB_AUTH_BASIC_ENABLE) {
    if (s_auth_mutex == NULL) {
        s_auth_mutex = xSemaphoreCreateMutex();
        if (s_auth_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create auth mutex");
            return ESP_ERR_NO_MEM;  // ✓ Signaler l'erreur plutôt que continuer
        }
    }
    
    // ... rest of init
}
```

---

# RÉSUMÉ DES CORRECTIONS

| Problème | Fichier | Ligne | Criticité | Correction |
|----------|---------|-------|-----------|-----------|
| Race condition shared_listeners | uart_bms.cpp | 1081 | CRITIQUE | Ajouter mutex |
| Race condition driver_started | can_victron.c | 997 | CRITIQUE | Protéger avec mutex |
| Deadlock portMAX_DELAY | web_server.c | 3396 | CRITIQUE | Utiliser timeout |
| Race condition channel_deadlines | can_publisher.c | 407 | ÉLEVÉE | Protéger tableau |
| TOCTOU unsubscribe | event_bus.c | 158 | ÉLEVÉE | Nettoyer sous lock |
| TOCTOU on_bms_update | can_publisher.c | 238 | ÉLEVÉE | Re-vérifier null |
| Fuite mutex | can_victron.c | 1012 | ÉLEVÉE | Vérifier avant delete |
| Race latest_bms | monitoring.c | 36 | MOYENNE | Ajouter mutex |
| Division par zéro | monitoring.c | 382 | MOYENNE | ✓ Déjà correct |
| Buffer overflow | event_bus.c | 269 | MOYENNE | Valider null-term |
| Code mort | uart_bms.cpp | 1063 | FAIBLE | Utiliser readback |
| État inconsistent | can_victron.c | 997 | MOYENNE | Voir correction #2 |
| Mutex nullptr | web_server.c | 473 | MOYENNE | Retourner erreur |

---

# RECOMMANDATIONS PRIORITAIRES

1. **IMMÉDIAT** (24h):
   - Corriger race condition sur s_shared_listeners (#1)
   - Corriger deadlock portMAX_DELAY (#3)
   - Corriger race condition s_driver_started (#2)

2. **COURT TERME** (1 semaine):
   - Corriger TOCTOU issues (#5, #6)
   - Ajouter synchronisation s_channel_deadlines (#4)
   - Ajouter mutex pour s_latest_bms (#8)

3. **MOYEN TERME** (2-3 semaines):
   - Valider tous les null-terminated strings
   - Audit complet des deadlock risks
   - Tests de stress sous charge

