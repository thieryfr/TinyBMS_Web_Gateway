# Plan de refactoring Phase 4

## Objectif

Découper les fichiers volumineux en modules plus maintenables :
- web_server.c (3507 lignes) → 5 fichiers
- config_manager.c (2781 lignes) → 5 fichiers

## 1. web_server.c → 5 fichiers

### Découpage proposé

```
web_server.c (3507 lignes)
    ↓
├── web_server_core.c          (~800 lignes)  - Init, lifecycle, registration
├── web_server_api.c           (~1200 lignes) - REST API endpoints
├── web_server_auth.c          (~700 lignes)  - Authentication, CSRF
├── web_server_static.c        (~400 lignes)  - Static file serving
└── web_server_websocket.c     (~400 lignes)  - WebSocket handlers
```

### web_server_internal.h (nouveau)

Header interne partagé contenant :
- Déclarations fonctions communes
- Constantes configuration
- État global (extern)

### web_server_core.c

**Responsabilités** :
- Initialisation serveur HTTP/HTTPS
- Lifecycle (start/stop)
- Enregistrement routes
- Mutex global et lock helpers
- Security headers
- Event publisher

**Fonctions principales** :
- `web_server_init()`
- `web_server_stop()`
- `web_server_set_event_publisher()`
- `web_server_lock()` / `web_server_unlock()`
- `web_server_set_security_headers()`

**Variables globales** :
- `g_server` (httpd_handle_t)
- `g_server_mutex`
- `g_event_publisher`

### web_server_api.c

**Responsabilités** :
- Tous les endpoints REST API `/api/*`
- Handlers GET/POST pour config, status, metrics
- OTA upload handler
- System restart handler

**Endpoints** :
- GET `/api/status` - État système
- GET `/api/config` - Configuration
- POST `/api/config` - Mise à jour config
- GET `/api/mqtt/config` - Config MQTT
- POST `/api/mqtt/config` - MAJ config MQTT
- POST `/api/ota/upload` - Upload OTA
- POST `/api/system/restart` - Redémarrage
- GET `/api/metrics/runtime` - Métriques runtime
- GET `/api/event-bus/metrics` - Métriques event bus
- GET `/api/system/tasks` - Tasks FreeRTOS
- GET `/api/system/modules` - Modules info

### web_server_auth.c

**Responsabilités** :
- HTTP Basic Authentication
- CSRF tokens (génération, validation)
- Rate limiting integration
- Credential loading (NVS)
- Password hashing (SHA-256)

**Fonctions principales** :
- `web_server_auth_init()`
- `web_server_require_authorization()`
- `web_server_basic_authenticate()`
- `web_server_issue_csrf_token()`
- `web_server_validate_csrf_token()`
- `web_server_send_unauthorized()`
- `web_server_send_forbidden()`
- Handlers CSRF: GET `/api/security/csrf`

**Variables globales** :
- `g_auth_mutex`
- `g_basic_auth_enabled`
- `s_basic_auth_username`
- `s_basic_auth_salt`
- `s_basic_auth_hash`
- `s_csrf_tokens[]`

### web_server_static.c

**Responsabilités** :
- Serveur fichiers statiques SPIFFS
- Content-type detection
- Caching headers
- 404 handling
- Index.html serving

**Fonctions principales** :
- `web_server_static_get_handler()`
- Helper fonctions pour MIME types

### web_server_websocket.c

**Responsabilités** :
- 4 WebSocket endpoints
- Client tracking (open/close)
- Broadcast helpers
- Frame handling

**Endpoints WebSocket** :
- `/ws/telemetry` - Données BMS temps réel
- `/ws/events` - Événements système
- `/ws/uart` - Données UART brutes
- `/ws/can` - Frames CAN

**Fonctions principales** :
- `web_server_telemetry_ws_handler()`
- `web_server_events_ws_handler()`
- `web_server_uart_ws_handler()`
- `web_server_can_ws_handler()`

## 2. config_manager.c → 5 fichiers

### Découpage proposé

```
config_manager.c (2781 lignes)
    ↓
├── config_manager_core.c      (~600 lignes)  - Init, load/save NVS
├── config_manager_validation.c (~700 lignes) - Validators
├── config_manager_json.c      (~600 lignes)  - JSON import/export
├── config_manager_mqtt.c      (~400 lignes)  - MQTT config
└── config_manager_network.c   (~500 lignes)  - WiFi/network config
```

### config_manager_internal.h (nouveau)

Header interne partagé contenant :
- Déclarations validation functions
- Constantes limites
- État global (extern)

### config_manager_core.c

**Responsabilités** :
- Initialisation module
- Load/save NVS
- Get/set configuration
- Mutex management
- Event publishing (CONFIG_UPDATED)

**Fonctions principales** :
- `config_manager_init()`
- `config_manager_get_config()`
- `config_manager_save_to_nvs()`
- `config_manager_load_from_nvs()`
- `config_manager_reset_to_defaults()`

**Variables globales** :
- `s_config` (tinybms_config_t)
- `s_config_mutex`

### config_manager_validation.c

**Responsabilités** :
- Validation toutes configurations
- Range checks
- Format validation (URI, SSID, etc.)
- Cohérence (min < max)
- Error messages

**Fonctions principales** :
- `config_validate_mqtt_broker_uri()`
- `config_validate_wifi_ssid()`
- `config_validate_voltage_limits()`
- `config_validate_temperature_limits()`
- `config_validate_poll_interval()`
- `config_validate_complete()`

### config_manager_json.c

**Responsabilités** :
- Import configuration depuis JSON
- Export configuration vers JSON
- Parsing cJSON
- Génération JSON
- Error handling JSON

**Fonctions principales** :
- `config_manager_update_from_json()`
- `config_manager_export_to_json()`
- `config_parse_mqtt_section()`
- `config_parse_wifi_section()`
- `config_parse_alert_section()`

### config_manager_mqtt.c

**Responsabilités** :
- Configuration MQTT spécifique
- Validation broker URI
- Credentials MQTT
- Keepalive settings
- Test connexion MQTT

**Fonctions principales** :
- `config_get_mqtt_config()`
- `config_set_mqtt_config()`
- `config_validate_mqtt_config()`
- `config_test_mqtt_connection()`

### config_manager_network.c

**Responsabilités** :
- Configuration WiFi
- Configuration réseau (hostname, IP)
- Validation SSID/password
- WiFi mode (station/AP)

**Fonctions principales** :
- `config_get_wifi_config()`
- `config_set_wifi_config()`
- `config_validate_wifi_config()`
- `config_get_network_config()`

## 3. Migration et compatibilité

### Headers publics

**web_server.h** : Inchangé, API publique reste identique
**config_manager.h** : Inchangé, API publique reste identique

### Headers internes

**web_server_internal.h** : Nouveau, usage interne uniquement
**config_manager_internal.h** : Nouveau, usage interne uniquement

### CMakeLists.txt

Mise à jour pour inclure tous les nouveaux fichiers source :

```cmake
# main/web_server/CMakeLists.txt
idf_component_register(
    SRCS
        "web_server_core.c"
        "web_server_api.c"
        "web_server_auth.c"
        "web_server_static.c"
        "web_server_websocket.c"
        "web_server_alerts.c"
        "auth_rate_limit.c"
        "https_config.c"
    INCLUDE_DIRS "."
    REQUIRES ...
)

# main/config_manager/CMakeLists.txt
idf_component_register(
    SRCS
        "config_manager_core.c"
        "config_manager_validation.c"
        "config_manager_json.c"
        "config_manager_mqtt.c"
        "config_manager_network.c"
    INCLUDE_DIRS "."
    REQUIRES ...
)
```

### Tests de compilation

Après chaque découpage :
1. `idf.py build` - Vérifier compilation
2. Résoudre erreurs de liens
3. Vérifier warnings
4. Tests fonctionnels basiques

## 4. Bénéfices attendus

### Maintenabilité

- **Avant** : Fichiers 3000+ lignes, difficiles à naviguer
- **Après** : Fichiers 400-800 lignes, responsabilités claires

### Temps de compilation

- Build incrémental plus rapide (seuls fichiers modifiés recompilés)

### Code reviews

- Reviews plus faciles (changements isolés par responsabilité)
- Conflits git réduits

### Tests

- Tests unitaires plus faciles (modules isolés)
- Mocking simplifié

### Complexité

| Métrique | Avant | Après | Amélioration |
|----------|-------|-------|--------------|
| Taille max fichier | 3507 lignes | ~1200 lignes | -66% |
| Complexité cyclomatique | Élevée | Moyenne | -40% |
| Temps navigation | 2-3 min | 30 sec | -75% |
| Maintenabilité | 6/10 | 9/10 | +50% |

## 5. Timeline

### Phase 4A : web_server.c (20-30h)

1. Créer web_server_internal.h ✅
2. Extraire web_server_auth.c (8h)
3. Extraire web_server_static.c (4h)
4. Extraire web_server_websocket.c (4h)
5. Extraire web_server_api.c (8h)
6. Créer web_server_core.c (4h)
7. Tests compilation et fonctionnels (2h)

### Phase 4B : config_manager.c (20-30h)

1. Créer config_manager_internal.h (2h)
2. Extraire config_manager_validation.c (8h)
3. Extraire config_manager_json.c (6h)
4. Extraire config_manager_mqtt.c (4h)
5. Extraire config_manager_network.c (4h)
6. Créer config_manager_core.c (4h)
7. Tests compilation et fonctionnels (2h)

**Total estimé** : 40-60 heures

## 6. Risques et mitigations

### Risques

1. **Breakage compilation** : Dépendances manquantes
   - Mitigation : Build fréquent après chaque extraction

2. **Régression fonctionnelle** : Comportement changé
   - Mitigation : Tests manuels après chaque module

3. **Performance dégradée** : Appels fonction supplémentaires
   - Mitigation : Inline pour fonctions critiques

4. **Conflits git** : Travail parallèle
   - Mitigation : Branch dédiée, merge rapide

### Tests de non-régression

Après refactoring complet :
- [ ] Compilation sans warnings
- [ ] Web UI fonctionne (GET/POST /api/config)
- [ ] Authentification fonctionne
- [ ] WebSocket alerts fonctionne
- [ ] OTA upload fonctionne
- [ ] Configuration NVS load/save OK
- [ ] MQTT config OK
- [ ] WiFi config OK

## 7. Documentation à mettre à jour

Après refactoring :
- ARCHITECTURE.md : Mise à jour structure web_server et config_manager
- MODULES.md : Mise à jour références fichiers
- DEVELOPMENT.md : Ajout info refactoring

---

**Plan validé** : Prêt pour implémentation Phase 4
