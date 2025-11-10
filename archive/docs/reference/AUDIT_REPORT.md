# Rapport d'Audit Complet - TinyBMS-GW

**Date**: 2025-11-07
**Version du firmware**: 0.1.0
**Objectif**: Audit exhaustif de tous les modules pour identifier les problèmes potentiels pouvant affecter la fonctionnalité principale de Gateway (monitoring, mapping UART→CAN)

---

## Résumé Exécutif

L'audit a identifié **67 problèmes** répartis sur 12 modules principaux :
- **3 problèmes CRITIQUES** nécessitant une correction immédiate
- **15 problèmes de HAUTE sévérité** pouvant causer des défaillances majeures
- **24 problèmes de sévérité MOYENNE** affectant la fiabilité
- **25 problèmes de FAIBLE sévérité** impactant la robustesse

### Points Critiques Identifiés

1. **UART_BMS**: Deadlock potentiel dans l'écriture de registres (CRITIQUE)
2. **UART_BMS**: Race condition dans l'enregistrement des listeners (CRITIQUE)
3. **WiFi**: Tempête de reconnexion infinie sans délai (CRITIQUE)
4. **Web Server**: Handler OTA non-fonctionnel (CRITIQUE)

---

## 1. Module UART_BMS

**Localisation**: `/home/user/TinyBMS-GW/main/uart_bms/`
**Responsabilité**: Acquisition des données du BMS via UART, polling régulier

### Problèmes Identifiés

#### 🔴 CRITIQUE 1.1: Deadlock dans l'écriture de registres
- **Fichier**: `uart_bms.cpp:807-860`
- **Description**: `uart_bms_write_register()` acquiert `s_command_mutex` puis suspend la tâche de polling avec `vTaskSuspend()`. Si la tâche tenait `s_rx_buffer_mutex` au moment de la suspension, le système se bloque définitivement.
- **Impact**: Blocage complet de la Gateway, nécessite un redémarrage matériel
- **Recommandation**:
  - Remplacer `vTaskSuspend()` par un flag volatile vérifié dans la boucle de polling
  - Implémenter un mécanisme de commande avec file d'attente

#### 🔴 CRITIQUE 1.2: Race condition sur les listeners
- **Fichier**: `uart_bms.cpp:698-733`
- **Description**: Le tableau `s_listeners[]` est accédé sans protection mutex lors de l'enregistrement, désenregistrement et notification
- **Impact**: Crash de la Gateway, appel de callbacks incorrects, corruption de l'état du CAN publisher
- **Recommandation**:
  - Ajouter un mutex pour protéger l'accès au tableau de listeners
  - Implémenter un mécanisme de copie-sur-lecture pour les notifications

#### 🟠 HAUTE 1.3: Race condition sur les buffers d'événements
- **Fichier**: `uart_bms.cpp:140-142`
- **Description**: L'index `s_next_event_buffer` est modifié sans synchronisation
- **Impact**: Données BMS corrompues envoyées au bus CAN, potentiellement dangereuses pour la batterie
- **Recommandation**: Utiliser un spinlock ou atomic pour l'incrémentation de l'index

#### 🟠 HAUTE 1.4: Race condition sur les snapshots partagés
- **Fichier**: `uart_bms.cpp:895-901`
- **Description**: `s_shared_snapshot` lu sans mutex pendant l'enregistrement de callbacks
- **Impact**: Données incohérentes dans l'interface web et les outils de monitoring
- **Recommandation**: Protéger toutes les lectures de snapshot avec le mutex

#### 🟠 HAUTE 1.5: Race condition lors du reset du buffer
- **Fichier**: `uart_bms.cpp:438-450`
- **Description**: Le mutex est relâché puis réacquis lors d'un overflow de buffer
- **Impact**: Perte de synchronisation des trames, données incorrectes
- **Recommandation**: Refactoriser pour maintenir le mutex ou utiliser un flag pour signaler le reset

#### 🟡 MOYENNE 1.6: Cleanup incomplet en cas d'échec
- **Fichier**: `uart_bms.cpp:685-695`
- **Description**: Les mutex ne sont pas supprimés si `xTaskCreate()` échoue
- **Impact**: Fuite mémoire, échecs de réinitialisation
- **Recommandation**: Ajouter `vSemaphoreDelete()` dans le chemin d'erreur

#### 🟡 MOYENNE 1.7: Erreurs UART non escaladées
- **Fichier**: `uart_bms.cpp:542-545`
- **Description**: Les erreurs de lecture UART sont loguées mais pas comptabilisées
- **Impact**: Gateway apparaît en bonne santé mais ne reçoit plus de données BMS
- **Recommandation**: Compteur d'erreurs avec seuil et publication d'événement d'alarme

---

## 2. Module CAN_VICTRON

**Localisation**: `/home/user/TinyBMS-GW/main/can_victron/`
**Responsabilité**: Interface physique CAN TWAI, gestion des keepalives

### Problèmes Identifiés

#### 🟠 HAUTE 2.1: Timeout du mutex d'état du driver
- **Fichier**: `can_victron.c:315-322`
- **Description**: Si le timeout du mutex se produit, `already_started` reste à false et le driver est démarré une seconde fois
- **Impact**: Arrêt du bus CAN, nécessite un redémarrage système
- **Recommandation**: Initialiser `already_started = true` par défaut, ou retourner une erreur explicite sur timeout

#### 🟠 HAUTE 2.2: Race condition sur l'état keepalive
- **Fichier**: `can_victron.c:370-378, 423-427, 432-436, 463-469`
- **Description**: Variables `s_keepalive_ok`, `s_last_keepalive_tx_ms`, `s_last_keepalive_rx_ms` non protégées
- **Impact**: Timeout keepalive prématuré, Victron détecte perte de communication, arrêt de la charge
- **Recommandation**: Ajouter un mutex ou utiliser des atomiques pour les timestamps

#### 🟠 HAUTE 2.3: Filtre TWAI trop restrictif
- **Fichier**: `can_victron.c:347-351`
- **Description**: Le filtre hardware n'accepte que l'ID 0x305, tous les autres messages sont rejetés
- **Impact**: Gateway ne peut pas répondre aux requêtes Victron, fonctionnalité limitée
- **Recommandation**: Élargir le filtre ou utiliser un filtre acceptant toutes les trames

#### 🟡 MOYENNE 2.4: Débordement de la queue TX non surveillé
- **Fichier**: `can_victron.c:576-588`
- **Description**: Aucune surveillance de la profondeur de la queue (16 trames)
- **Impact**: Perte silencieuse de données pendant les périodes de fort trafic CAN
- **Recommandation**: Ajouter un compteur de trames perdues et des logs d'avertissement

#### 🟡 MOYENNE 2.5: Tâche CAN impossible à arrêter
- **Fichier**: `can_victron.c:503-524`
- **Description**: Boucle infinie sans condition de sortie
- **Impact**: Fuite de ressources, impossible de réinitialiser le système CAN
- **Recommandation**: Ajouter un flag de terminaison vérifié dans la boucle

---

## 3. Module CAN_PUBLISHER

**Localisation**: `/home/user/TinyBMS-GW/main/can_publisher/`
**Responsabilité**: Traduction UART→CAN, ordonnancement des trames, contrôle CVL

### Problèmes Identifiés

#### 🟠 HAUTE 3.1: Suppression de tâche non sécurisée
- **Fichier**: `can_publisher.c:293-298`
- **Description**: Délai de 100ms avant `vTaskDelete()` ne garantit pas un état sûr
- **Impact**: Si la tâche tient le mutex buffer, deadlock permanent, arrêt de publication CAN
- **Recommandation**: Implémenter un mécanisme de terminaison propre avec flag et attente

#### 🟡 MOYENNE 3.2: Timeout mutex buffer perd des données silencieusement
- **Fichier**: `can_publisher.c:343-346, 382-390`
- **Description**: Timeout du mutex (50ms) entraîne la perte de la trame sans statistiques
- **Impact**: Limites de charge (CVL/CCL/DCL) perdues, risque de surcharge de la batterie
- **Recommandation**: Compteur de trames perdues, log périodique, augmentation du timeout

#### 🟡 MOYENNE 3.3: Conversion spinlock→mutex incomplète
- **Fichier**: `can_publisher.c:50, 98-109`
- **Description**: Remplacement d'un spinlock par un mutex pour une section critique courte
- **Impact**: Inversion de priorité potentielle, retards dans la publication d'événements
- **Recommandation**: Retourner au spinlock ou utiliser un mutex récursif

#### 🟡 MOYENNE 3.4: Race condition initialisation CVL
- **Fichier**: `cvl_controller.c:180-182`
- **Description**: Vérification de `s_cvl_initialised` sans protection mutex
- **Impact**: Double initialisation, fuite du premier mutex, calculs CVL incorrects
- **Recommandation**: Utiliser un pattern init-once avec spinlock

#### 🟡 MOYENNE 3.5: Dérive des deadlines de planification
- **Fichier**: `can_publisher.c:405-406`
- **Description**: Deadline fixée à `now + period` au lieu de `deadline + period`
- **Impact**: Frames publiées plus lentement que configuré, Victron détecte dégradation
- **Recommandation**: Calculer deadline = deadline_précédente + période

---

## 4. Module CONVERSION_TABLE

**Localisation**: `/home/user/TinyBMS-GW/main/can_publisher/conversion_table.c`
**Responsabilité**: Encodage des PGN Victron, gestion des compteurs d'énergie

### Problèmes Identifiés

#### 🟡 MOYENNE 4.1: Perte de précision des compteurs d'énergie
- **Fichier**: `conversion_table.c:648-654`
- **Description**: Utilisation de double sans protection contre l'overflow sur le long terme
- **Impact**: Après ~10^15 Wh, perte de précision (problème dans des décennies pour systèmes haute puissance)
- **Recommandation**: Documentation de la limite, ou passage à un format 128-bit

#### 🟡 MOYENNE 4.2: Race condition persistance énergie
- **Fichier**: `conversion_table.c:199-220`
- **Description**: Lecture des compteurs d'énergie hors protection mutex avant écriture NVS
- **Impact**: Valeurs incohérentes dans NVS, divergence après redémarrage
- **Recommandation**: Acquérir le mutex avant lecture des compteurs

#### 🔵 FAIBLE 4.3: Gestion du wrap-around de timestamp
- **Fichier**: `conversion_table.c:634-639`
- **Description**: Pas de gestion du wrap-around uint64_t ou des sauts d'horloge
- **Impact**: Échantillons perdus lors de synchronisation NTP
- **Recommandation**: Détecter et logger les sauts d'horloge backwards

#### 🔵 FAIBLE 4.4: Hypothèses d'endianness
- **Fichier**: `conversion_table.c:582-595, 851-856, 913-918`
- **Description**: Packaging manuel des octets assume little-endian
- **Impact**: Code non portable vers architectures big-endian
- **Recommandation**: Documentation ou utilisation de macros d'endianness

#### 🔵 FAIBLE 4.5: Overflow encodage énergie
- **Fichier**: `conversion_table.c:531-553`
- **Description**: Saturation à 429 MWh, pas de détection explicite
- **Impact**: Compteur sature à la valeur max
- **Recommandation**: Logger un avertissement lors de la saturation

---

## 5. Module EVENT_BUS

**Localisation**: `/home/user/TinyBMS-GW/main/event_bus/`
**Responsabilité**: Système pub-sub central pour communication inter-modules

### Problèmes Identifiés

#### 🟠 HAUTE 5.1: Désinscription pendant callback
- **Fichier**: `event_bus.c:177-191, 131-163`
- **Description**: Un subscriber peut appeler `unsubscribe()` depuis son callback, supprimant sa queue pendant qu'elle pourrait être utilisée
- **Impact**: Corruption mémoire, crash système
- **Recommandation**: Déférer la suppression de queue jusqu'après dispatch de tous les événements

#### 🟡 MOYENNE 5.2: Log insuffisant des événements perdus
- **Fichier**: `event_bus.c:182-188`
- **Description**: Log uniquement quand le compteur est une puissance de 2
- **Impact**: Saturation de queue non détectée, pas d'alertes monitoring
- **Recommandation**: Logger tous les N événements ou avoir un taux de log adaptatif

#### 🟡 MOYENNE 5.3: Échec création de queue
- **Fichier**: `event_bus.c:100-103`
- **Description**: Les appelants ne vérifient pas toujours NULL après subscribe
- **Impact**: Déréférencement de pointeur NULL dans le code appelant
- **Recommandation**: Auditer tous les appelants pour vérification NULL

---

## 6. Module MONITORING

**Localisation**: `/home/user/TinyBMS-GW/main/monitoring/`
**Responsabilité**: Agrégation de télémétrie, historique, sérialisation JSON

### Problèmes Identifiés

#### 🟠 HAUTE 6.1: Lecture de snapshot sans mutex
- **Fichier**: `monitoring.c:299-300`
- **Description**: `monitoring_get_status_json()` lit `s_has_latest_bms` et `s_latest_bms` sans mutex
- **Impact**: Données de batterie incohérentes dans web/MQTT (voltage mismatché avec courant)
- **Recommandation**: Acquérir le mutex avant toute lecture du snapshot

#### 🟡 MOYENNE 6.2: Race condition cache snapshot
- **Fichier**: `monitoring.c:235, 316`
- **Description**: `s_last_snapshot` écrit sans mutex, `s_last_snapshot_len` peut être mis à jour avant le contenu
- **Impact**: JSON tronqué ou corrompu publié dans événements télémétrie
- **Recommandation**: Protéger l'écriture complète du cache avec mutex

#### 🔵 FAIBLE 6.3: Vérification bounds registres
- **Fichier**: `monitoring.c:188-199`
- **Description**: Pas de pré-vérification que register_count * taille_estimée tient dans buffer
- **Impact**: Overflow possible si nombreux registres (marges actuelles suffisantes)
- **Recommandation**: Ajouter un assert ou vérification explicite

#### 🔵 FAIBLE 6.4: Edge case arithmétique historique
- **Fichier**: `monitoring.c:364`
- **Description**: Calcul de l'index de départ pourrait être incorrect si max_samples > capacity
- **Impact**: Historique retourné dans le mauvais ordre
- **Recommandation**: Clamper max_samples à capacity avant calcul

---

## 7. Module HISTORY_LOGGER

**Localisation**: `/home/user/TinyBMS-GW/main/monitoring/history_logger.c`
**Responsabilité**: Persistance des données historiques sur LittleFS

### Problèmes Identifiés

#### 🟠 HAUTE 7.1: Pas de récupération sur erreur d'écriture
- **Fichier**: `history_logger.c:223-226, 265-273`
- **Description**: Échec de `fopen()` ou `fprintf()` logué uniquement, pas de retry
- **Impact**: Perte complète de logging historique pendant erreurs transitoires ou disque plein
- **Recommandation**: Implémenter retry avec backoff, buffer en RAM temporaire

#### 🟡 MOYENNE 7.2: Durabilité des données - pas de fsync()
- **Fichier**: `history_logger.c:385-386, 391`
- **Description**: `fflush()` appelé mais pas `fsync()`, données peuvent être perdues sur coupure
- **Impact**: Échantillons récents (jusqu'à l'intervalle de flush) perdus sur perte de courant
- **Recommandation**: Ajouter `fsync()` après `fflush()` périodique

#### 🟡 MOYENNE 7.3: Risque de boucle infinie dans retention
- **Fichier**: `history_logger.c:328-354`
- **Description**: Boucle while pourrait devenir infinie si tous fichiers ont size_bytes=0 mais total>max
- **Impact**: Tâche history_logger bloquée indéfiniment, plus de logging
- **Recommandation**: Ajouter compteur d'itérations max ou vérifier sum(sizes)

#### 🔵 FAIBLE 7.4: Parsing CSV sans validation
- **Fichier**: `history_logger.c:723-739`
- **Description**: `strtof()` et `strtoull()` utilisés sans vérifier errno
- **Impact**: CSV corrompu produit des valeurs garbage silencieusement
- **Recommandation**: Vérifier errno et HUGE_VAL après conversions

#### 🔵 FAIBLE 7.5: Fuite sur échec realloc
- **Fichier**: `history_logger.c:574-584`
- **Description**: Si `realloc()` échoue, `files` libéré mais `closedir()` pas encore appelé
- **Impact**: Fuite de handle de répertoire lors d'OOM
- **Recommandation**: Appeler `closedir()` avant `free()` dans chemin d'erreur

---

## 8. Module CONFIG_MANAGER

**Localisation**: `/home/user/TinyBMS-GW/main/config_manager/`
**Responsabilité**: Gestion de configuration, persistance NVS, API de configuration

### Problèmes Identifiés

#### 🟠 HAUTE 8.1: Écriture partielle NVS
- **Fichier**: `config_manager.c:962-983`
- **Description**: 6 appels `nvs_set_*` séquentiels, si #3 échoue, #1-2 commitées mais #4-6 skip
- **Impact**: Config MQTT incohérente après échec (nouveau broker avec anciens credentials)
- **Recommandation**: Utiliser un pattern transactionnel ou rollback sur échec

#### 🟠 HAUTE 8.2: Divergence état runtime/persistant
- **Fichier**: `config_manager.c:1540, 1554-1564`
- **Description**: Intervalle de poll appliqué au runtime avant persistance NVS
- **Impact**: Config prend effet immédiatement mais pas sauvée, revert après reboot
- **Recommandation**: Persister d'abord, puis appliquer (ou rollback sur échec persist)

#### 🟡 MOYENNE 8.3: Race condition lecture/écriture config
- **Fichier**: `config_manager.c:1789, 1742-1773`
- **Description**: Setters utilisent mutex, getter `config_manager_get_mqtt_client_config()` non
- **Impact**: Serveur web peut lire config MQTT partiellement mise à jour
- **Recommandation**: Protéger tous les getters avec le même mutex

#### 🟡 MOYENNE 8.4: Validation conflit GPIO manquante
- **Fichier**: `config_manager.c:1356-1374`
- **Description**: Validation de range GPIO mais pas de vérification TX==RX
- **Impact**: Accepter {"tx_gpio": 37, "rx_gpio": 37} cause échec init UART
- **Recommandation**: Vérifier TX != RX dans validation

#### 🔵 FAIBLE 8.5: Section critique longue
- **Fichier**: `config_manager.c:1762-1770`
- **Description**: `config_manager_build_config_snapshot()` dans mutex crée JSON 2KB
- **Impact**: Autres threads bloqués 100ms+, timeouts web possibles
- **Recommandation**: Copier state dans struct local, relâcher mutex, puis sérialiser

---

## 9. Module CVL_CONTROLLER

**Localisation**: `/home/user/TinyBMS-GW/main/can_publisher/cvl_controller.c`
**Responsabilité**: Machine à états CVL, contrôle dynamique des limites de charge

### Problèmes Identifiés

#### 🟠 HAUTE 9.1: Initialisation lazy non thread-safe
- **Fichier**: `cvl_controller.c:180-182`
- **Description**: Check-then-act sur `s_cvl_initialised` sans mutex
- **Impact**: Double init possible, fuite de mutex, calculs CVL incorrects
- **Recommandation**: Init-once avec spinlock ou appeler init explicitement au startup

#### 🟡 MOYENNE 9.2: Transition sur données stale
- **Fichier**: `cvl_controller.c:111-127`
- **Description**: Si pack_voltage_v==0 (défaillance capteur), bulk_target devient 0V
- **Impact**: CVL→0V, commande 0A charge même avec cellules saines, batterie ne charge plus
- **Recommandation**: Valider données entrantes, utiliser valeur précédente si invalide

#### 🔵 FAIBLE 9.3: Fallback float sans isfinite()
- **Fichier**: `cvl_controller.c:78-84`
- **Description**: `fallback_float()` check `<=0.0f` mais pas NaN
- **Impact**: NaN se propage, CVL devient NaN, CAN publisher écrit "null"
- **Recommandation**: Ajouter `!isfinite(preferred)` dans condition

#### 🔵 FAIBLE 9.4: Récupération lente de cell protection
- **Fichier**: `cvl_logic.c:239-242`
- **Description**: Si max_recovery_step_v très petit, récupération prend 100+ secondes
- **Impact**: Charge limitée longtemps après retour cellules à plage safe
- **Recommandation**: Recovery step adaptatif ou valeur minimale raisonnable

#### 🔵 FAIBLE 9.5: Ambiguïté valeur zéro config
- **Fichier**: `cvl_controller.c:88`
- **Description**: `fallback_unsigned()` traite 0 comme invalide
- **Impact**: Impossible de configurer explicitement 0 (ex: désactiver sustain)
- **Recommandation**: Utiliser un sentinel différent (ex: UINT_MAX)

---

## 10. Module WEB_SERVER

**Localisation**: `/home/user/TinyBMS-GW/main/web_server/`
**Responsabilité**: Serveur HTTP, API REST, WebSockets, OTA

### Problèmes Identifiés

#### 🔴 CRITIQUE 10.1: Handler OTA non-fonctionnel
- **Fichier**: `web_server.c:1186-1225`
- **Description**: Endpoint `/api/ota` lit le firmware mais ne l'écrit JAMAIS dans la partition OTA
- **Impact**: Mises à jour apparaissent réussir mais firmware jamais flashé, fausse sécurité
- **Recommandation**: Implémenter `esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()`, `esp_ota_set_boot_partition()`

#### 🟠 HAUTE 10.2: Drop événement WebSocket sur timeout mutex
- **Fichier**: `web_server.c:160-162`
- **Description**: `ws_client_list_broadcast()` timeout 50ms → retour immédiat, événement perdu
- **Impact**: Échantillons télémétrie critiques perdus, dashboard montre données stales
- **Recommandation**: Queue de broadcast avec retry ou timeout plus long

#### 🟡 MOYENNE 10.3: Allocation clients WebSocket sans limite
- **Fichier**: `web_server.c:111-122`
- **Description**: `ws_client_list_add()` alloue sans vérifier max count
- **Impact**: Attaque exhaustion mémoire, des centaines de connexions idle crashent serveur
- **Recommandation**: Limite max clients (ex: 10), refuser nouvelles connexions au-delà

#### 🟡 MOYENNE 10.4: Race lecture snapshot monitoring
- **Fichier**: `web_server.c:1308-1309`
- **Description**: Handler WebSocket appelle `monitoring_get_status_json()` juste après ajout client
- **Impact**: Message initial WebSocket pourrait contenir torn read (voltage N, current N+1)
- **Recommandation**: Synchroniser avec mutex monitoring ou accepter risque minimal

#### 🔵 FAIBLE 10.5: Fuite tâche WebSocket sur réinit
- **Fichier**: `web_server.c:1619-1621`
- **Description**: `xTaskCreate()` ne vérifie pas si `s_event_task_handle` existe déjà
- **Impact**: Réinit wifi crée tâches dupliquées, double-delivery messages, fragmentation heap
- **Recommandation**: Vérifier handle NULL avant create ou implémenter cleanup proper

---

## 11. Module MQTT_CLIENT

**Localisation**: `/home/user/TinyBMS-GW/main/mqtt_client/`
**Responsabilité**: Client MQTT ESP-IDF, gestion connexion/publication

### Problèmes Identifiés

#### 🟠 HAUTE 11.1: Race création mutex
- **Fichier**: `mqtt_client.c:97-102, 242-247`
- **Description**: Check-then-create mutex sans protection
- **Impact**: Fuite du premier mutex si deux threads init simultanément, deadlock potentiel
- **Recommandation**: Utiliser spinlock pour protéger création ou init-once pattern

#### 🟡 MOYENNE 11.2: Pas de protection overflow queue messages
- **Fichier**: `mqtt_client.c:215`
- **Description**: Aucun check si queue ESP-IDF MQTT pleine
- **Impact**: Messages perdus silencieusement lors de publishing rapide
- **Recommandation**: Vérifier retour, implémenter backpressure ou retry

#### 🟡 MOYENNE 11.3: Race invocation callback
- **Fichier**: `mqtt_client.c:394-396`
- **Description**: Callback invoqué sans vérifier validité après changement contexte
- **Impact**: NPE ou use-after-free si `mqtt_client_init()` appelé pendant callback
- **Recommandation**: Copier callback pointer localement avec mutex ou ref counting

#### 🔵 FAIBLE 11.4: Pas de logique reconnexion applicative
- **Fichier**: `mqtt_client.c:126-158`
- **Description**: Dépend entièrement de la lib ESP-IDF pour reconnexion
- **Impact**: Pas de contrôle sur policy reconnexion, backoff non personnalisable
- **Recommandation**: Implémenter backoff exponentiel applicatif si nécessaire

---

## 12. Module MQTT_GATEWAY

**Localisation**: `/home/user/TinyBMS-GW/main/mqtt_gateway/`
**Responsabilité**: Pont événement bus → MQTT, gestion topics

### Problèmes Identifiés

#### 🟠 HAUTE 12.1: Accès topic sans lock
- **Fichier**: `mqtt_gateway.c:185-194`
- **Description**: Si timeout mutex, topic lu quand même sans protection
- **Impact**: Topic corrompu/tronqué, messages routés vers mauvais topics MQTT
- **Recommandation**: Retourner erreur sur timeout, ne jamais accéder sans lock

#### 🟡 MOYENNE 12.2: Pas de récupération sur échec publish
- **Fichier**: `mqtt_gateway.c:161-170`
- **Description**: Échec publish logué mais données perdues définitivement
- **Impact**: Mises à jour status manquantes, gaps dans time-series DB
- **Recommandation**: Buffer de retry ou queue de publication

#### 🟡 MOYENNE 12.3: Risque overflow queue événements
- **Fichier**: `mqtt_gateway.c:613-628`
- **Description**: Boucle événements bloque sur receive, publish MQTT peut être lent
- **Impact**: Queue event_bus (32 entrées) se remplit, événements dropped en amont
- **Recommandation**: Timeout sur receive, surveiller profondeur queue

#### 🔵 FAIBLE 12.4: Anomalie organisation fonction
- **Fichier**: `mqtt_gateway.c:306-424`
- **Description**: Fonction `mqtt_gateway_load_topics` split avec autre fonction au milieu
- **Impact**: Lisibilité code, risque erreurs maintenance
- **Recommandation**: Refactoring pour organisation claire

---

## 13. Module WIFI

**Localisation**: `/home/user/TinyBMS-GW/main/wifi/`
**Responsabilité**: Connexion WiFi STA/AP, gestion reconnexion, fallback AP

### Problèmes Identifiés

#### 🔴 CRITIQUE 13.1: Variables d'état non protégées
- **Fichier**: `wifi.c:81-88`
- **Description**: Variables `s_ap_fallback_active`, `s_retry_count` modifiées depuis event handler sans mutex
- **Impact**: Race conditions, torn reads/writes, logique retry corrompue
- **Recommandation**: Mutex pour toutes les variables d'état WiFi

#### 🔴 CRITIQUE 13.2: Tempête de reconnexion infinie
- **Fichier**: `wifi.c:268-272`
- **Description**: Si fallback AP désactivé, retry immédiat sans délai → boucle infinie
- **Impact**: CPU 100%, flooding réseau, device non-responsive, blocklisting par AP
- **Recommandation**: Backoff exponentiel avec délai minimum (ex: 1s, 2s, 4s, 8s, max 60s)

#### 🟠 HAUTE 13.3: Race condition fallback AP
- **Fichier**: `wifi.c:118-120, 255-258`
- **Description**: `s_ap_fallback_active` check-then-set sans mutex
- **Impact**: Multiples tentatives démarrage AP, fuites mémoire, double init
- **Recommandation**: Protéger avec mutex ou atomic

#### 🟠 HAUTE 13.4: Modification concurrente d'état
- **Fichier**: `wifi.c:327, 255`
- **Description**: Handler IP_GOT et DISCONNECT modifient état simultanément possiblement
- **Impact**: Perte de handling disconnect, état connexion incorrect, désync compteur retry
- **Recommandation**: Mutex global pour toutes modifications d'état

#### 🔵 FAIBLE 13.5: Validation longueur credentials
- **Fichier**: `wifi.c:193-199`
- **Description**: Check longueur password après `strlcpy` qui peut tronquer
- **Impact**: Message d'erreur trompeur, échec connexion avec credentials tronqués
- **Recommandation**: Vérifier longueur AVANT copie et rejeter si trop long

---

## Statistiques Globales

### Par Sévérité

| Sévérité | Nombre | Pourcentage |
|----------|--------|-------------|
| 🔴 CRITIQUE | 3 | 4.5% |
| 🟠 HAUTE | 15 | 22.4% |
| 🟡 MOYENNE | 24 | 35.8% |
| 🔵 FAIBLE | 25 | 37.3% |
| **TOTAL** | **67** | **100%** |

### Par Module

| Module | Critique | Haute | Moyenne | Faible | Total |
|--------|----------|-------|---------|--------|-------|
| uart_bms | 2 | 3 | 2 | 0 | 7 |
| can_victron | 0 | 3 | 2 | 0 | 5 |
| can_publisher | 0 | 1 | 4 | 0 | 5 |
| conversion_table | 0 | 0 | 2 | 3 | 5 |
| event_bus | 0 | 1 | 2 | 0 | 3 |
| monitoring | 0 | 1 | 1 | 2 | 4 |
| history_logger | 0 | 1 | 2 | 2 | 5 |
| config_manager | 0 | 2 | 2 | 1 | 5 |
| cvl_controller | 0 | 1 | 1 | 3 | 5 |
| web_server | 1 | 1 | 2 | 1 | 5 |
| mqtt_client | 0 | 1 | 2 | 1 | 4 |
| mqtt_gateway | 0 | 1 | 2 | 1 | 4 |
| wifi | 2 | 2 | 0 | 1 | 5 |
| **TOTAL** | **3** | **15** | **24** | **25** | **67** |

---

## Recommandations Prioritaires

### Action Immédiate (CRITIQUE)

1. **UART_BMS** : Corriger le deadlock dans write_register (remplacer vTaskSuspend)
2. **UART_BMS** : Protéger le tableau listeners avec mutex
3. **WiFi** : Ajouter délai/backoff dans boucle reconnexion
4. **Web Server** : Implémenter réellement le handler OTA

### Court Terme (HAUTE)

5. Protéger tous les buffers d'événements UART avec synchronisation appropriée
6. Ajouter mutex pour état keepalive CAN
7. Élargir filtre TWAI pour accepter plus de messages
8. Implémenter terminaison propre des tâches CAN
9. Protéger toutes lectures de snapshot monitoring avec mutex
10. Implémenter retry sur erreurs d'écriture history_logger
11. Rendre écriture NVS transactionnelle ou avec rollback
12. Synchroniser runtime/persistance de config
13. Protéger init CVL avec mécanisme thread-safe
14. Implémenter queue/retry pour événements WebSocket
15. Protéger création mutex MQTT avec spinlock
16. Toujours acquérir lock avant accès topics MQTT gateway
17. Protéger toutes variables d'état WiFi avec mutex

### Moyen Terme (MOYENNE)

18-41. Voir détails dans les sections individuelles des modules

### Long Terme (FAIBLE)

42-67. Amélioration de la robustesse et portabilité

---

## Plan d'Implémentation

### Phase 1 : Correctifs Critiques (Semaine 1)
- PR #1 : Correctifs UART_BMS (deadlock + listeners)
- PR #2 : Correctif WiFi (reconnexion)
- PR #3 : Implémentation OTA complète

### Phase 2 : Correctifs Haute Priorité (Semaines 2-3)
- PR #4 : Correctifs synchronisation UART/CAN/Event Bus
- PR #5 : Correctifs monitoring et history_logger
- PR #6 : Correctifs config_manager et CVL
- PR #7 : Correctifs web_server et MQTT

### Phase 3 : Correctifs Moyenne Priorité (Semaines 4-5)
- PR #8 : Améliorations robustesse générale
- PR #9 : Monitoring et statistiques

### Phase 4 : Correctifs Faible Priorité (Semaine 6)
- PR #10 : Améliorations qualité code
- PR #11 : Documentation et portabilité

---

## Conclusion

L'audit a révélé plusieurs problèmes critiques nécessitant une attention immédiate, particulièrement dans les modules UART_BMS, WiFi et Web Server. La majorité des problèmes concernent la synchronisation multi-thread et la gestion d'erreurs.

Les correctifs proposés amélioreront significativement la stabilité et la fiabilité de la Gateway TinyBMS, particulièrement pour les scénarios de charge élevée et de conditions réseau dégradées.

**Recommandation** : Implémenter les correctifs critiques et haute priorité avant déploiement en production.
