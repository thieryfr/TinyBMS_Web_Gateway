# TinyBMS Web Gateway

Squelette de projet ESP-IDF pour la passerelle TinyBMS ↔ Victron avec interface web embarquée. Cette arborescence prépare l'intégration des différents modules (UART BMS, CAN Victron, MQTT, monitoring, etc.) ainsi que la partie front-end servie depuis l'ESP32.

## Structure du projet
```
TinyBMS-WebGateway/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── include/
│   │   └── app_config.h
│   ├── event_bus/
│   ├── uart_bms/
│   ├── can_victron/
│   ├── pgn_mapper/
│   ├── web_server/
│   ├── config_manager/
│   ├── mqtt_client/
│   └── monitoring/
├── web/
├── test/
├── docs/
├── .gitignore
├── README.md
└── idf_component.yml
```

Chaque sous-répertoire `main/<module>` contient un `CMakeLists.txt` dédié et des stubs C/C++ prêts à être complétés.

## Premiers pas
1. Installer l'ESP-IDF v5.x.
2. Configurer le projet :
   ```bash
   idf.py set-target esp32
   idf.py menuconfig
   ```
3. Compiler et flasher :
   ```bash
   idf.py build
   idf.py flash monitor
   ```

## Architecture logicielle
Le firmware est organisé en couches :

- **Acquisition** : `uart_bms` récupère les trames TinyBMS et normalise les mesures dans `uart_bms_live_data_t`.
- **Services** : `pgn_mapper`, `can_publisher` et `can_victron` assemblent les PGN Victron (0x351, 0x355, 0x356, etc.) et orchestrent les timers/keepalive CAN.【F:main/pgn_mapper/pgn_mapper.c†L1-L41】【F:main/can_victron/can_victron.c†L1-L125】
- **Connectivité** : `web_server`, `mqtt_client`, `wifi` et `monitoring` exposent les données aux clients distants et au front-end web.
- **Infrastructures** : `event_bus` assure la communication inter-tâches et `config_manager` applique les paramètres NVS/`menuconfig`.

Une description détaillée (diagrammes de flux, responsabilités par tâche, contraintes de temps réel) est maintenue dans `docs/architecture.md` et doit être relue lors de toute évolution majeure.【F:docs/architecture.md†L1-L36】

## PGN Victron & conversions TinyBMS
Les conversions TinyBMS → Victron s'appuient sur le tableau `main/can_publisher/conversion_table.c` et les définitions de `docs/bridge_pgn_defs.h`. Chaque PGN encode des échelles spécifiques :

- **0x351 CVL/CCL/DCL** : tension en 0,1 V, courants en 0,1 A ; limites dynamiques basées sur les registres TinyBMS et les éventuelles réductions logicielles.
- **0x355 SOC/SOH** : pourcentage sur 1 % à partir des registres d'état TinyBMS.
- **0x356 Tension/Courant** : tension pack en 0,01 V, courant en 0,1 A signé.
- **0x35A Alarmes** : bits d'états pour surtension, sous-tension, température, etc.
- **0x35E/0x371/0x382** : chaînes ASCII (fabricant, nom batterie, famille) extraites des registres TinyBMS lorsque disponibles, sinon des constantes `CONFIG_TINYBMS_CAN_*`.
- **0x35F** : identification matérielle (ID modèle, firmware public/interne, capacité en service) directement lue dans les registres TinyBMS 0x01F4/0x01F5/0x01F6/0x0132.
- **0x378/0x379** : compteurs d'énergie cumulée et capacité installée.

Le détail des champs, sources TinyBMS et formules de conversion est consolidé dans `docs/pgn_conversions.md`, qui complète la feuille `docs/pgn_mapping.xlsx` pour les besoins d'intégration Victron.【F:docs/pgn_conversions.md†L1-L126】

## Configuration & compilation
### Prérequis
- ESP-IDF v5.x installé avec les dépendances Python (voir [documentation officielle](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)).
- Chaîne d'outils Xtensa-esp32 et CMake disponibles via `export.sh` ou `idf.py --version`.
- Python ≥3.10 pour les scripts et tests.
- (Optionnel) Node.js ≥18 si l'on doit reconstruire les assets du dossier `web/`.

### Étapes de build
1. Initialiser l'environnement ESP-IDF :
   ```bash
   . $IDF_PATH/export.sh
   idf.py --version
   ```
2. Sélectionner la cible et ajuster la configuration :
   ```bash
   idf.py set-target esp32
   idf.py menuconfig
   ```
   Les options `Component config → TinyBMS Gateway` regroupent les paramètres `CONFIG_TINYBMS_*` (GPIO CAN, keepalive, Wi-Fi STA/AP, identifiants Victron, etc.).【F:main/can_victron/can_victron.c†L38-L125】【F:main/wifi/wifi.c†L22-L370】【F:main/can_publisher/conversion_table.c†L32-L702】
3. Compiler et empaqueter l'image :
   ```bash
   idf.py build
   ```
4. Flasher et monitorer :
   ```bash
   idf.py flash monitor
   ```

Pour mettre à jour la partie web, modifier `web/` puis lancer `idf.py build` : les fichiers sont automatiquement intégrés à la partition SPIFFS.

## Tests & mise en production
Les campagnes de tests (unitaires, intégration CAN, essais sur banc Victron) sont décrites dans `docs/operations.md`. On y retrouve :

- Les commandes `idf.py test`, `idf.py -T <target> flash monitor` et les scénarios de validation CAN/keepalive.
- La procédure de pré-production (capture CAN, export PGN, seuils d'alarmes).
- Les critères d'acceptation avant déploiement terrain.

La mise en production standard suit la check-list `docs/operations.md#mise-en-production` avec vérification des versions `sdkconfig.defaults`, configuration Wi-Fi et sauvegarde des logs CAN.

## Documentation
- `docs/architecture.md` : description détaillée de l'architecture logicielle et des flux de données.
- `docs/pgn_conversions.md` : mapping PGN TinyBMS ↔ Victron et conversions d'unités.
- `docs/pgn_mapping.xlsx` : feuille de calcul pour l'intégration CAN.
- `docs/api_endpoints.md` : documentation des endpoints REST/WebSocket.
- `docs/operations.md` : build, tests, validation et processus de mise en production.

## Interface web
Les assets statiques sont disponibles dans `web/`. Ils seront intégrés dans une partition SPIFFS et servis via le module `web_server`.
