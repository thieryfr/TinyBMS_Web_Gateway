# Analyse TinyBMS-GW (sécurité non incluse)

## 1. Détection de bugs et erreurs

### Critique

#### 1. Événements MQTT métriques partagent un buffer global mutable
- **Localisation** : `main/mqtt/tiny_mqtt_publisher.c` (construction du message et publication dans l'évènement bus).【F:main/mqtt/tiny_mqtt_publisher.c†L356-L408】
- **Description** : `tiny_mqtt_publisher_on_bms_update()` réutilise la structure globale `s_message` et son tampon `s_payload_buffer` pour toutes les publications. L'évènement bus transporte uniquement un pointeur vers cette structure. Si plusieurs échantillons sont placés en file avant d'être consommés (file de 32 éléments dans `mqtt_gateway`), tous les évènements référencent le même tampon qui est écrasé à chaque itération. Les consommateurs reçoivent alors un contenu incohérent (données du dernier échantillon uniquement) voire un JSON partiellement formé si l'écriture est interrompue.
- **Impact** : Corruption de données de télémétrie et métriques MQTT. Les tableaux de bord ou automatisations recevront des séries temporelles erronées, et la traçabilité historique sera compromise. Le problème est aggravé en cas de latence réseau ou de saturation du bus d'évènements.
- **Solution proposée** : Dupliquer la charge utile pour chaque publication (par exemple via un pool circulaire de messages) ou envoyer un `uart_bms_live_data_t` immuable et laisser le consommateur sérialiser. Exemple minimal :
  ```c
  // Avant
  s_message.payload = s_payload_buffer;
  event.payload = &s_message;

  // Après (exemple avec double-buffer)
  tiny_mqtt_message_t *slot = allocate_message();
  memcpy(slot->payload, s_payload_buffer, payload_length);
  event.payload = slot;
  event.payload_size = sizeof(*slot);
  ```
  Libérer chaque slot une fois publié par `mqtt_gateway`.

#### 2. Timer de reconnexion STA jamais démarré
- **Localisation** : `wifi_schedule_sta_retry()` et appels associés (`wifi_start_ap_mode`, `wifi_sta_retry_timer_callback`).【F:main/wifi/wifi.c†L165-L178】【F:main/wifi/wifi.c†L294-L320】
- **Description** : `wifi_schedule_sta_retry()` se contente d'appeler `xTimerChangePeriod()` sur un timer initialement à l'arrêt. FreeRTOS ne démarre pas un timer dormant via `xTimerChangePeriod()`. En cas de bascule en mode AP de secours, aucune tentative de reconnexion STA n'est donc reprogrammée.
- **Impact** : Le gateway reste indéfiniment en mode AP de secours même si le réseau STA revient, à moins d'un redémarrage manuel. La passerelle perd sa connectivité principale et toutes les fonctionnalités dépendantes du réseau (MQTT, OTA, supervision distante).
- **Solution proposée** : Démarrer explicitement le timer. Exemple :
  ```c
  xTimerStop(s_sta_retry_timer, 0);
  if (xTimerChangePeriod(s_sta_retry_timer, delay_ticks, 0) == pdPASS) {
      if (xTimerStart(s_sta_retry_timer, 0) != pdPASS) {
          ESP_LOGW(TAG, "Failed to start STA retry timer");
      }
  }
  ```
  S'assurer que le timer est bien en mode one-shot pour éviter une boucle infinie.

### Élevée

#### 3. Flag AP fallback bloqué lors d'un échec de création d'interface
- **Localisation** : `wifi_start_ap_mode()` lors de la création de `s_ap_netif`.【F:main/wifi/wifi.c†L214-L238】
- **Description** : Le drapeau `s_ap_fallback_active` est positionné avant de créer l'interface AP. Si `esp_netif_create_default_wifi_ap()` échoue, la fonction retourne immédiatement sans réinitialiser ce flag. Le système croit alors que l'AP de secours est actif alors qu'aucune interface n'est disponible.
- **Impact** : Les chemins conditionnant la logique sur `s_ap_fallback_active` (arrêt des tentatives STA, resynchronisation des LED, timer de retry) considèrent l'AP opérationnel. La station ne retente plus de connexion et l'interface de secours reste indisponible, entraînant une perte de service durable.
- **Solution proposée** : En cas d'échec, remettre `s_ap_fallback_active` à `false` (sous mutex) avant de retourner et publier un évènement d'erreur pour déclencher une stratégie de repli (ex. remontée d'alarme).

### Moyenne

#### 4. Couplage fort et complexité croissante dans `wifi.c`
- **Localisation** : `main/wifi/wifi.c` (~750 lignes mêlant état, callbacks d'évènements, timers, configuration AP/STA, publication bus).【F:main/wifi/wifi.c†L1-L733】
- **Description** : La logique Wi-Fi agrège multiples responsabilités (gestion STA/AP, métadonnées d'évènements, retry, timers) dans un fichier monolithique. L'absence de séparation claire rend difficile la vérification des invariants (ex. état du fallback, mutex utilisés) et augmente le risque de régressions lors de corrections.
- **Impact** : Maintenabilité réduite et temps de revue élevé. Les bugs identifiés ci-dessus illustrent la difficulté à raisonner sur les transitions d'état.
- **Solution proposée** : Scinder en sous-modules (p. ex. `wifi_fallback.c`, `wifi_events.c`), introduire une machine d'état explicite et centraliser la gestion des timers/mutex via une structure unique passée aux fonctions. Ajouter des tests unitaires sur la logique de retry via stubs ESP-IDF.

## 3. Qualité du code
- Les modules `app_main.c` et `wifi.c` contiennent de longues fonctions procédurales. Introduire des helpers dédiés (ex. `wifi_update_state()`), documenter les pré/post-conditions et renforcer les tests (cf. existants dans `test/`).【F:main/app_main.c†L184-L218】【F:main/wifi/wifi.c†L1-L733】
- Plusieurs modules manipulent directement des buffers JSON via `snprintf` (MQTT, historique). Centraliser la sérialisation afin d'éviter les divergences de format et faciliter les évolutions.【F:main/mqtt/mqtt_gateway.c†L200-L286】【F:main/mqtt/tiny_mqtt_publisher.c†L356-L379】

## 4. Performances
- Le bus d'évènements (`event_bus_publish`) sérialise toutes les publications derrière un mutex global et peut bloquer jusqu'à `timeout` sur `xQueueSend`. Sous charge (queues pleines), les producteurs suivants sont gelés et la latence augmente.【F:main/event_bus/event_bus.c†L132-L199】 Prévoir une file lock-free par abonné ou relâcher le mutex avant l'attente bloquante.
- `history_logger_process_sample()` force `fflush` et `fsync` périodiquement. Sur flash SPIFFS/LittleFS, `fsync` est coûteux ; s'assurer que `CONFIG_TINYBMS_HISTORY_FLUSH_INTERVAL` reste raisonnable et envisager un mode asynchrone (file d'écriture dédiée).【F:main/monitoring/history_logger.c†L392-L432】

## 5. Propositions d'amélioration
- Corriger les bugs critiques ci-dessus (buffer MQTT, timers STA, flag fallback).
- Factoriser la sérialisation JSON pour les messages CAN/MQTT et historisation.
- Introduire une machine d'état documentée pour le Wi-Fi avec tests unitaires couvrant les transitions STA⇄AP.
- Compléter les métriques systèmes avec un vrai compteur de redémarrages (persistant via NVS) pour fiabiliser les diagnostics.

## 6. Résumé exécutif
- **Gravité principale** : Deux défauts critiques dans la chaîne Wi-Fi/MQTT compromettent la disponibilité réseau et l'intégrité des métriques.
- **Tendances** : Architecture modulaire et instrumentation déjà en place, mais plusieurs modules (Wi-Fi, MQTT publisher) doivent être durcis côté concurrence.
- **Priorité immédiate** : Sécuriser les événements MQTT, fiabiliser la reconnexion STA et la gestion du fallback AP.

**Note globale de qualité (hors sécurité)** : 6.5 / 10 — base solide, mais des corrections urgentes sont nécessaires sur la pile connectivité pour garantir la stabilité en production.
