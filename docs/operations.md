# Guide opérations TinyBMS Web Gateway

Ce document regroupe les instructions pour construire, tester et déployer le firmware, ainsi que la boucle de validation avec l'équipe.

## 1. Préparation de l'environnement
1. Installer l'ESP-IDF v5.x et sa chaîne d'outils (Xtensa, CMake, Ninja).
2. Cloner ce dépôt et initialiser les sous-modules éventuels.
3. Sourcing de l'environnement :
   ```bash
   . $IDF_PATH/export.sh
   idf.py --version  # doit afficher la version ESP-IDF détectée
   python --version  # ≥ 3.10
   ```
4. (Optionnel) Installer Node.js ≥18 et `npm install` si l'on modifie `web/` et qu'on souhaite exécuter des outils front-end.
5. Vérifier que `idf.py list-targets` contient `esp32`.

## 2. Configuration du projet
1. Sélectionner la cible :
   ```bash
   idf.py set-target esp32
   ```
2. Lancer `idf.py menuconfig` et revoir :
   - **Component config → TinyBMS Gateway** : GPIO CAN (`CONFIG_TINYBMS_CAN_VICTRON_*`), temporisations keepalive (`CONFIG_TINYBMS_CAN_KEEPALIVE_*`).
   - **Component config → Wi-Fi** : SSID/PASS station et AP (`CONFIG_TINYBMS_WIFI_*`).
   - **Component config → TinyBMS CAN metadata** : chaînes `CONFIG_TINYBMS_CAN_MANUFACTURER`, `CONFIG_TINYBMS_CAN_BATTERY_NAME`, `CONFIG_TINYBMS_CAN_BATTERY_FAMILY`.
   - **Serial flasher config** : vitesse et port.
3. Exporter la configuration :
   ```bash
   idf.py save-defconfig
   ```
   Committer `sdkconfig` uniquement pour des builds reproductibles (sinon s'appuyer sur `sdkconfig.defaults`).

## 3. Build & artefacts
```bash
idf.py build
```
Les artefacts sont générés dans `build/` :
- `tinybms-web-gateway.bin` : application principale.
- `partition-table.bin`, `bootloader/bootloader.bin`.
- Image SPIFFS avec le contenu de `web/`.

Pour reconstruire les assets web, modifier `web/*` puis relancer `idf.py build`. Les fichiers sont compressés et intégrés automatiquement.

## 4. Tests
### 4.1 Tests unitaires (hôte)
```bash
idf.py test
```
- Exécute les suites Unity définies dans `test/` (conversion PGN, event bus, UART).
- Les rapports sont visibles dans `build/<test>/test.log`.

### 4.2 Tests sur matériel
1. Connecter l'ESP32 au PC via USB.
2. Flasher et ouvrir le monitor :
   ```bash
   idf.py flash monitor
   ```
3. Observer :
   - Logs `can_victron` (keepalive OK, publication PGN).
   - Logs `wifi` (connexion STA ou fallback AP).
4. Capturer le trafic CAN avec un analyseur (peakCAN, CANable). Commande type :
   ```bash
   canlogserver -p socketcan0 -f capture.log
   ```
   Vérifier les PGN présents et les valeurs avec `docs/pgn_conversions.md`.

### 4.3 Validation Victron (banc)
Se référer au plan `docs/testing/validation_plan.md` pour les scénarios :
- Variation SOC/SOH.
- Déclenchement alarmes (sous/surtension, température).
- Vérification des compteurs énergie.

## 5. Mise en production
1. Geler la configuration (`sdkconfig.defaults`, fichiers `config.json` dans `docs/`).
2. Générer les artefacts avec `idf.py build` sur une machine propre.
3. Flasher l'appareil cible :
   ```bash
   idf.py -p /dev/ttyUSB0 --baud 460800 flash
   ```
4. Sauvegarder les logs initiaux (fichier `monitor.log`) pour traçabilité.
5. Réaliser un test fonctionnel court : lecture PGN 0x351/0x355/0x356, accès UI web, ping MQTT si configuré.
6. Documenter la version :
   - Hash Git.
   - Version ESP-IDF.
   - Date de déploiement et site.
7. Archiver les artefacts (`build/`, `sdkconfig`, capture CAN) dans l'espace partagé de l'équipe.

## 5bis. Plan de déploiement OTA

### Fenêtre de déploiement
- **Production** : fenêtre principale le mercredi 14/08/2024 entre 09h00 et 12h00 CET (faible trafic).
- **Pré-production** : répétition générale la veille (13/08/2024) sur les gateways pilotes.
- **Gel des changements** : aucun merge sur la branche `release` 48 h avant la fenêtre production.

### Procédure OTA
1. Publier l'image `tinybms-web-gateway.bin` sur le serveur OTA sécurisé (`ota.tinybms.lan`) avec un numéro de version incrémental.
2. Mettre à jour le manifeste OTA (`ota/manifest.json`) en ajoutant la nouvelle entrée (hash SHA256, version, URL binaire).
3. Déclencher la campagne via l'orchestrateur (`tools/ota/deploy.py --group production --version X.Y.Z`).
4. Surveiller la télémétrie (`mqtt_bms/#/ota`) pour confirmer la progression (<5 % d'échecs attendus).

### Plan de rollback
- Conserver la version précédente disponible dans le manifeste et marquée `fallback`.
- En cas d'erreur >5 % ou bug critique, exécuter `deploy.py --group production --version <précédente>` dans la même fenêtre.
- Informer l'équipe Ops et QA via le canal `#tinybms-operations` pour suivi et post-mortem.

### Communication
- **Avant** : envoyer le mémo de pré-déploiement 48 h avant (rappel fenêtre, périmètre, impacts) aux équipes Firmware, Tests, Support.
- **Pendant** : publier un fil dédié sur `#tinybms-operations` avec état d'avancement toutes les 30 min.
- **Après** : diffuser un compte rendu (succès, incidents, temps de déploiement) et mettre à jour `CHANGELOG.md` + Confluence.

## 6. Révision & validation équipe
1. Préparer un résumé des changements (architecture, PGN, conversions, configuration) et pointer vers les sections mises à jour :
   - `README.md` pour la vue d'ensemble.
   - `docs/architecture.md` pour les détails techniques.
   - `docs/pgn_conversions.md` pour les formules.
   - `docs/testing/validation_plan.md` pour les essais.
2. Organiser une revue avec :
   - Développeurs firmware (validation conversion & tâches FreeRTOS).
   - Intégrateur Victron (validation PGN/valeurs).
   - Responsable QA (validation tests & checklist mise en production).
3. Collecter les retours dans un ticket partagé et itérer jusqu'à validation.
4. Une fois approuvé, tagger la release (`git tag vX.Y.Z`) et mettre à jour le changelog.

## 7. Ressources complémentaires
- [ESP-IDF build system](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)
- [Documentation Victron CAN-bus BMS](docs/VictCan-bus_bms_protocol20210417.pdf)
- [Référence PGN interne](docs/reference/victron_pgn_signal_summary.md)

