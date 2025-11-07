# Plan de Correctifs TinyBMS-GW

Ce document détaille les correctifs proposés suite à l'audit complet du firmware.

---

## PR #1 : Correctifs Critiques UART_BMS

### Problème 1.1 : Deadlock dans uart_bms_write_register()

**Changements proposés** :

1. **Remplacer vTaskSuspend/Resume par un flag** :
```cpp
// Ajouter au niveau global
static volatile bool s_poll_pause_requested = false;

// Dans uart_bms_write_register() (lignes 811-813, 857-859)
s_poll_pause_requested = true;
// Attendre que la tâche de poll confirme la pause
vTaskDelay(pdMS_TO_TICKS(50));

// cleanup:
s_poll_pause_requested = false;
```

2. **Modifier la boucle de polling** :
```cpp
// Dans uart_bms_poll_task (ligne ~540)
while (true) {
    // Vérifier flag de pause
    while (s_poll_pause_requested) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ... reste du code de polling
}
```

**Avantages** :
- Pas de suspension forcée de tâche
- La tâche peut terminer proprement son cycle actuel
- Évite le deadlock avec s_rx_buffer_mutex

---

### Problème 1.2 : Race Condition sur Listeners

**Changements proposés** :

1. **Ajouter un mutex pour les listeners** :
```cpp
// Au niveau global (après ligne 79)
static SemaphoreHandle_t s_listeners_mutex = nullptr;

// Dans uart_bms_init() (après création autres mutex)
s_listeners_mutex = xSemaphoreCreateMutex();
```

2. **Protéger l'enregistrement** (lignes 698-719) :
```cpp
esp_err_t uart_bms_register_listener(...) {
    if (xSemaphoreTake(s_listeners_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Code d'enregistrement existant

    xSemaphoreGive(s_listeners_mutex);
    return ESP_OK;
}
```

3. **Protéger la notification** (lignes 119-126) :
```cpp
static void uart_bms_notify_listeners(const uart_bms_live_data_t *data) {
    // Copier les callbacks dans un buffer local sous mutex
    ListenerEntry local_listeners[UART_BMS_LISTENER_SLOTS];

    if (xSemaphoreTake(s_listeners_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(local_listeners, s_listeners, sizeof(local_listeners));
        xSemaphoreGive(s_listeners_mutex);
    } else {
        return; // Skip notification si mutex timeout
    }

    // Invoquer callbacks en dehors du mutex
    for (size_t i = 0; i < UART_BMS_LISTENER_SLOTS; ++i) {
        if (local_listeners[i].callback != nullptr) {
            local_listeners[i].callback(data, local_listeners[i].context);
        }
    }
}
```

---

### Problème 1.3 : Race Condition Event Buffer Index

**Changements proposés** :

```cpp
// Remplacer lignes 140-142
portENTER_CRITICAL(&s_event_buffer_lock);
uart_bms_live_data_t* storage = &s_event_buffers[s_next_event_buffer];
s_next_event_buffer = (s_next_event_buffer + 1U) % UART_BMS_EVENT_BUFFERS;
portEXIT_CRITICAL(&s_event_buffer_lock);

*storage = *data;
```

**Ajouter au niveau global** :
```cpp
static portMUX_TYPE s_event_buffer_lock = portMUX_INITIALIZER_UNLOCKED;
```

---

### Problème 1.6 : Cleanup Incomplet

**Changements proposés dans uart_bms_init()** (lignes 685-695) :

```cpp
// Dans le cas d'échec de xTaskCreate
if (xTaskCreate(...) != pdPASS) {
    ESP_LOGE(kTag, "Failed to create UART polling task");

    // AJOUT: Nettoyer les mutex
    if (s_command_mutex != nullptr) {
        vSemaphoreDelete(s_command_mutex);
        s_command_mutex = nullptr;
    }
    if (s_rx_buffer_mutex != nullptr) {
        vSemaphoreDelete(s_rx_buffer_mutex);
        s_rx_buffer_mutex = nullptr;
    }
    if (s_snapshot_mutex != nullptr) {
        vSemaphoreDelete(s_snapshot_mutex);
        s_snapshot_mutex = nullptr;
    }
    if (s_listeners_mutex != nullptr) {
        vSemaphoreDelete(s_listeners_mutex);
        s_listeners_mutex = nullptr;
    }

    uart_driver_delete(UART_BMS_UART_PORT);
    s_uart_initialised = false;
    return ESP_FAIL;
}
```

---

## PR #2 : Correctifs Critiques WiFi

### Problème 13.1 : Variables d'État Non Protégées

**Changements proposés** :

1. **Ajouter un mutex global WiFi** (après ligne 88) :
```cpp
static SemaphoreHandle_t s_wifi_state_mutex = NULL;
```

2. **Initialiser dans wifi_init()** :
```cpp
esp_err_t wifi_init(void) {
    if (s_wifi_state_mutex == NULL) {
        s_wifi_state_mutex = xSemaphoreCreateMutex();
        if (s_wifi_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    // ... reste du code
}
```

3. **Protéger tous les accès aux variables d'état** :
```cpp
// Exemple pour s_retry_count (lignes 255-257)
if (xSemaphoreTake(s_wifi_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_retry_count++;
    int current_retry = s_retry_count;
    xSemaphoreGive(s_wifi_state_mutex);

    if (current_retry < CONFIG_TINYBMS_WIFI_STA_MAX_RETRY) {
        // ... logique retry
    }
} else {
    ESP_LOGW(TAG, "Failed to acquire wifi state mutex");
}
```

---

### Problème 13.2 : Tempête de Reconnexion Infinie

**Changements proposées** (lignes 268-272) :

```cpp
#else
    // AVANT:
    // s_retry_count = 0;
    // wifi_attempt_connect();

    // APRÈS: Backoff exponentiel
    if (xSemaphoreTake(s_wifi_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_retry_count++;
        int retry = s_retry_count;
        xSemaphoreGive(s_wifi_state_mutex);

        // Backoff: 1s, 2s, 4s, 8s, 16s, 32s, max 60s
        uint32_t backoff_ms = 1000 << (retry < 6 ? retry : 6);
        if (backoff_ms > 60000) backoff_ms = 60000;

        ESP_LOGW(TAG, "Retry %d in %u ms", retry, backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        wifi_attempt_connect();
    } else {
        ESP_LOGE(TAG, "Cannot acquire mutex for retry logic");
    }
#endif
```

---

### Problème 13.3 : Race Condition Fallback AP

**Changements proposés** :

```cpp
// Dans wifi_start_ap_mode() (lignes 118-120)
if (xSemaphoreTake(s_wifi_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (s_ap_fallback_active) {
        xSemaphoreGive(s_wifi_state_mutex);
        return;
    }
    s_ap_fallback_active = true;
    xSemaphoreGive(s_wifi_state_mutex);
} else {
    ESP_LOGW(TAG, "Cannot start AP, mutex timeout");
    return;
}

// ... reste du code
```

---

## PR #3 : Correctifs CAN_VICTRON

### Problème 2.1 : Timeout Mutex État Driver

**Changements proposés** (lignes 315-322) :

```cpp
bool already_started = true;  // CHANGEMENT: Par défaut true
if (s_driver_state_mutex != NULL) {
    if (xSemaphoreTake(s_driver_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        already_started = s_driver_started;
        xSemaphoreGive(s_driver_state_mutex);
    } else {
        ESP_LOGW(TAG, "Driver state mutex timeout, assuming started");
        return ESP_ERR_TIMEOUT;  // CHANGEMENT: Retourner erreur
    }
}

if (already_started) {
    return ESP_OK;
}
```

---

### Problème 2.2 : Race Condition Keepalive

**Changements proposés** :

1. **Ajouter mutex keepalive** (après ligne 64) :
```cpp
static SemaphoreHandle_t s_keepalive_mutex = NULL;
```

2. **Initialiser dans can_victron_init()** :
```cpp
s_keepalive_mutex = xSemaphoreCreateMutex();
```

3. **Protéger tous les accès** :
```cpp
// Exemple dans can_victron_send_keepalive (ligne 423-427)
if (xSemaphoreTake(s_keepalive_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    s_last_keepalive_tx_ms = now;
    s_keepalive_ok = true;
    xSemaphoreGive(s_keepalive_mutex);
}

// Dans service_keepalive (lignes 459-469)
bool needs_recovery = false;
if (xSemaphoreTake(s_keepalive_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (!s_keepalive_ok && (now - s_last_keepalive_rx_ms > timeout_ms)) {
        needs_recovery = true;
    }
    xSemaphoreGive(s_keepalive_mutex);
}

if (needs_recovery) {
    // ... logique de recovery
}
```

---

### Problème 2.3 : Filtre TWAI Trop Restrictif

**Changements proposés** (lignes 347-351) :

```cpp
// OPTION 1: Accepter toutes les trames
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// OU OPTION 2: Filtre élargi pour plage Victron (0x300-0x3FF)
twai_filter_config_t f_config = {
    .acceptance_code = (uint32_t)(0x300 << 21),
    .acceptance_mask = ~(0x0FFU << 21),  // Accepte 0x300-0x3FF
    .single_filter = true,
};
```

---

### Problème 2.5 : Tâche Impossible à Arrêter

**Changements proposés** :

1. **Ajouter flag terminaison** (après ligne 62) :
```cpp
static volatile bool s_task_should_exit = false;
```

2. **Modifier boucle tâche** (lignes 503-524) :
```cpp
static void can_victron_task(void *context)
{
    (void)context;
    while (!s_task_should_exit) {  // CHANGEMENT
        if (!can_victron_is_driver_started()) {
            vTaskDelay(pdMS_TO_TICKS(CAN_VICTRON_TASK_DELAY_MS));
            continue;
        }

        // ... reste du code

        vTaskDelay(pdMS_TO_TICKS(CAN_VICTRON_TASK_DELAY_MS));
    }

    ESP_LOGI(TAG, "CAN task exiting");
    vTaskDelete(NULL);
}
```

3. **Fonction d'arrêt** :
```cpp
void can_victron_stop_task(void) {
    s_task_should_exit = true;
    // Attendre que la tâche se termine
    vTaskDelay(pdMS_TO_TICKS(200));
    s_can_task_handle = NULL;
}
```

---

## PR #4 : Correctifs CAN_PUBLISHER

### Problème 3.1 : Suppression Tâche Non Sécurisée

**Changements proposés** (lignes 293-298) :

```cpp
// Ajouter flag global
static volatile bool s_task_should_exit = false;

// Dans can_publisher_deinit()
if (s_publish_task_handle != NULL) {
    s_task_should_exit = true;  // Signaler arrêt

    // Attendre que tâche confirme arrêt (max 1s)
    for (int i = 0; i < 20 && s_publish_task_handle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (s_publish_task_handle != NULL) {
        ESP_LOGW(TAG, "Task did not exit gracefully, forcing delete");
        vTaskDelete(s_publish_task_handle);
    }
    s_publish_task_handle = NULL;
}

// Dans can_publisher_task (ligne ~360)
static void can_publisher_task(void *context) {
    while (!s_task_should_exit) {
        // ... code existant

        vTaskDelay(wait_ticks);
    }

    s_publish_task_handle = NULL;  // Signaler sortie
    vTaskDelete(NULL);
}
```

---

### Problème 3.5 : Dérive des Deadlines

**Changements proposés** (lignes 405-406) :

```cpp
// AVANT:
// s_channel_deadlines[i] = now + s_channel_period_ticks[i];

// APRÈS:
s_channel_deadlines[i] += s_channel_period_ticks[i];

// Si deadline dans le passé (ex: après longue pause), resynchroniser
if ((int32_t)(now - s_channel_deadlines[i]) > 0) {
    s_channel_deadlines[i] = now + s_channel_period_ticks[i];
}
```

---

## PR #5 : Correctifs Monitoring & History Logger

### Problème 6.1 : Lecture Snapshot Sans Mutex

**Changements proposés dans monitoring.c** (ligne 299-300) :

```cpp
const char *monitoring_get_status_json(void) {
    if (s_monitoring_mutex == NULL) {
        return "{}";
    }

    // AJOUT: Acquérir mutex
    if (xSemaphoreTake(s_monitoring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout reading status");
        return s_last_snapshot;  // Retourner dernier snapshot connu
    }

    bool has_data = s_has_latest_bms;
    uart_bms_live_data_t local_data = s_latest_bms;

    xSemaphoreGive(s_monitoring_mutex);

    if (!has_data) {
        return "{}";
    }

    // Construire JSON avec données locales
    monitoring_build_snapshot_json(&local_data, ...);
    return s_last_snapshot;
}
```

---

### Problème 7.1 : Pas de Récupération sur Erreur

**Changements proposés dans history_logger.c** :

1. **Ajouter buffer de retry en RAM** (après ligne 69) :
```cpp
#define HISTORY_RETRY_BUFFER_SIZE 32
static char s_retry_buffer[HISTORY_RETRY_BUFFER_SIZE][256];
static size_t s_retry_buffer_count = 0;
static SemaphoreHandle_t s_retry_mutex = NULL;
```

2. **Fonction de retry** :
```cpp
static void history_logger_retry_failed_writes(void) {
    if (s_retry_buffer_count == 0) return;

    if (!history_fs_is_mounted()) return;

    if (xSemaphoreTake(s_retry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (size_t i = 0; i < s_retry_buffer_count; i++) {
            // Tenter écriture
            if (history_logger_append_line(s_retry_buffer[i]) == ESP_OK) {
                // Succès, décaler buffer
                if (i < s_retry_buffer_count - 1) {
                    memmove(&s_retry_buffer[i], &s_retry_buffer[i+1],
                           (s_retry_buffer_count - i - 1) * 256);
                }
                s_retry_buffer_count--;
                i--;
            }
        }
        xSemaphoreGive(s_retry_mutex);
    }
}
```

3. **Modifier logique d'écriture** (lignes 265-273) :
```cpp
if (fprintf(s_active_file, "%s\n", line) < 0) {
    ESP_LOGW(TAG, "Failed to write line");

    // AJOUT: Ajouter au buffer de retry si pas plein
    if (xSemaphoreTake(s_retry_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_retry_buffer_count < HISTORY_RETRY_BUFFER_SIZE) {
            strlcpy(s_retry_buffer[s_retry_buffer_count], line, 256);
            s_retry_buffer_count++;
            ESP_LOGI(TAG, "Buffered failed write for retry (%zu in queue)",
                    s_retry_buffer_count);
        } else {
            ESP_LOGW(TAG, "Retry buffer full, dropping sample");
        }
        xSemaphoreGive(s_retry_mutex);
    }

    return ESP_FAIL;
}
```

---

### Problème 7.2 : Pas de fsync()

**Changements proposés** (après ligne 386) :

```cpp
if (s_active_file != NULL) {
    fflush(s_active_file);

    // AJOUT: Synchroniser avec disque
    int fd = fileno(s_active_file);
    if (fd >= 0) {
        if (fsync(fd) != 0) {
            ESP_LOGW(TAG, "fsync failed: %d", errno);
        }
    }
}
```

---

## PR #6 : Correctifs Config Manager & MQTT

### Problème 8.1 : Écriture Partielle NVS

**Changements proposés dans config_manager.c** (lignes 962-983) :

```cpp
static esp_err_t config_manager_store_mqtt_config_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_MANAGER_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // CHANGEMENT: Vérifier tous les set avant commit
    bool all_ok = true;

    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_URI_KEY,
                          s_mqtt_config.broker_uri) == ESP_OK);
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_USERNAME_KEY,
                          s_mqtt_config.username) == ESP_OK);
    all_ok &= (nvs_set_str(handle, CONFIG_MANAGER_MQTT_PASSWORD_KEY,
                          s_mqtt_config.password) == ESP_OK);
    all_ok &= (nvs_set_u16(handle, CONFIG_MANAGER_MQTT_KEEPALIVE_KEY,
                          s_mqtt_config.keepalive_sec) == ESP_OK);
    all_ok &= (nvs_set_u8(handle, CONFIG_MANAGER_MQTT_QOS_KEY,
                         s_mqtt_config.qos) == ESP_OK);
    all_ok &= (nvs_set_u8(handle, CONFIG_MANAGER_MQTT_RETAIN_KEY,
                         s_mqtt_config.retain ? 1 : 0) == ESP_OK);

    if (!all_ok) {
        ESP_LOGE(TAG, "Failed to set one or more MQTT config values");
        nvs_close(handle);
        return ESP_FAIL;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
```

---

### Problème 8.2 : Divergence Runtime/Persistant

**Changements proposés** (lignes 1540-1564) :

```cpp
// Dans config_manager_apply_config_payload()

// AVANT:
// uart_bms_set_poll_interval_ms(poll_interval);
// err = config_manager_store_poll_interval_to_nvs(poll_interval);

// APRÈS: Persister d'abord
esp_err_t err = config_manager_store_poll_interval_to_nvs(poll_interval);
if (err == ESP_OK) {
    uart_bms_set_poll_interval_ms(poll_interval);
    ESP_LOGI(TAG, "Applied and persisted poll interval: %u ms", poll_interval);
} else {
    ESP_LOGW(TAG, "Failed to persist poll interval, not applying to runtime");
}
```

---

### Problème 11.1 : Race Création Mutex MQTT

**Changements proposés dans mqtt_client.c** (lignes 97-102) :

```cpp
// Ajouter spinlock global
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;

void mqtt_client_init(...) {
    portENTER_CRITICAL(&s_init_lock);
    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_init_lock);

    if (s_ctx.lock == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT mutex");
        return ESP_ERR_NO_MEM;
    }

    // ... reste du code
}
```

---

### Problème 12.1 : Accès Topic Sans Lock

**Changements proposés dans mqtt_gateway.c** (lignes 185-194) :

```cpp
// SUPPRIMER le fallback sans lock
char topic[128] = {0};
bool retain_flag = false;

if (mqtt_gateway_lock_ctx(pdMS_TO_TICKS(100))) {  // AUGMENTER timeout à 100ms
    strncpy(topic, s_gateway.status_topic, sizeof(topic));
    topic[sizeof(topic) - 1U] = '\0';
    retain_flag = s_gateway.config.retain_enabled;
    mqtt_gateway_unlock_ctx();
} else {
    ESP_LOGW(TAG, "Failed to acquire gateway lock, aborting publish");
    return false;  // CHANGEMENT: Retourner erreur au lieu d'accéder sans lock
}
```

---

## Résumé des PRs

| PR | Titre | Modules | Criticité | Fichiers Modifiés |
|----|-------|---------|-----------|-------------------|
| #1 | fix(uart): correctifs critiques deadlock et race conditions | uart_bms | CRITIQUE | uart_bms.cpp, uart_bms.h |
| #2 | fix(wifi): correction tempête reconnexion et protection état | wifi | CRITIQUE | wifi.c |
| #3 | fix(can): correctifs synchronisation CAN victron | can_victron | HAUTE | can_victron.c |
| #4 | fix(can): amélioration robustesse CAN publisher | can_publisher | HAUTE | can_publisher.c |
| #5 | fix(monitoring): protection thread-safe et récupération erreurs | monitoring, history_logger | HAUTE | monitoring.c, history_logger.c |
| #6 | fix(config): transactions NVS et synchronisation MQTT | config_manager, mqtt_client, mqtt_gateway | HAUTE | config_manager.c, mqtt_client.c, mqtt_gateway.c |

---

## Ordre d'Implémentation Recommandé

1. **PR #1** (CRITIQUE) - Évite deadlocks système
2. **PR #2** (CRITIQUE) - Évite CPU 100% et device non-responsive
3. **PR #3** (HAUTE) - Stabilise communication CAN
4. **PR #4** (HAUTE) - Améliore fiabilité publication
5. **PR #5** (HAUTE) - Sécurise données monitoring
6. **PR #6** (HAUTE) - Fiabilise configuration

**Temps estimé total** : 2-3 semaines de développement + tests
