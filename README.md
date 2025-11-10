# TinyBMS Web Gateway

Squelette de projet ESP-IDF pour la passerelle TinyBMS ↔ Victron avec interface web embarquée. Cette arborescence prépare l'intégration des différents modules (UART BMS, CAN Victron, MQTT, monitoring, etc.) ainsi que la partie front-end servie depuis l'ESP32.

ESP32-S3-WROOM-1-N8R8 using an Xtensa® 32-bit LX7 CPU operating at up to 240 MHz (8MB flash, 8MB PSRAM), dual CAN bus support, two CAN bus transceivers.

https://wiki.autosportlabs.com/ESP32-CAN-X2#Introduction

https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/establish-serial-connection.html

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

La documentation a été réorganisée pour refléter l'architecture actuelle du projet :

### 📚 Documentation Principale (`docs/`)

- **[INDEX.md](docs/INDEX.md)** : Point d'entrée principal avec navigation par catégories
- **[QUICK_START.md](docs/QUICK_START.md)** : Guides rapides par rôle (Manager/Dev/Reviewer)
- **[SUMMARY_FR.md](docs/SUMMARY_FR.md)** : Résumé exécutif en français

### 🏗️ Architecture (`docs/architecture/`)

- **[AUDIT_REPORT.md](docs/architecture/AUDIT_REPORT.md)** : Rapport d'audit sécurité/conformité
- **[FILES_REFERENCE.md](docs/architecture/FILES_REFERENCE.md)** : Carte de navigation du code source
- **[uart_can_analysis.md](docs/uart_can_analysis.md)** : Analyse complète des interactions UART/CAN

### 🔌 Protocoles (`docs/protocols/`)

- **[DOCUMENTATION_COMMUNICATIONS.md](docs/protocols/DOCUMENTATION_COMMUNICATIONS.md)** : Référence complète des protocoles (Modbus, CAN, REST API, WebSocket)
- **[COMMUNICATION_REFERENCE.json](docs/protocols/COMMUNICATION_REFERENCE.json)** : Référence structurée JSON
- **[tinybms_register_can_flow.md](docs/tinybms_register_can_flow.md)** : Flux de données UART → CAN
- **[interaction_diagrams.md](docs/interaction_diagrams.md)** : Diagrammes de séquence détaillés

### 📖 Guides (`docs/guides/`)

- **[INTEGRATION_GUIDE.md](docs/guides/INTEGRATION_GUIDE.md)** : Procédures d'intégration
- **[ota.md](docs/ota.md)** : Mise à jour firmware OTA
- **[monitoring_diagnostics.md](docs/monitoring_diagnostics.md)** : Diagnostics et monitoring

### 📦 Archives (`archive/`)

- **reference/** : Documents historiques (PHASEs, plans, analyses obsolètes)
- **reports/** : Rapports d'audit français (référence historique)
- **docs/** : 54 fichiers de documentation archivés

## Interface web
Les assets statiques sont disponibles dans `web/`. Ils seront intégrés dans une partition SPIFFS et servis via le module `web_server`.
