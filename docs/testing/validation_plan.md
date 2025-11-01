# TinyBMS CAN Validation Plan

This document summarises the automated and manual validation activities introduced for the
TinyBMS web gateway. It complements the existing UART end-to-end tests by covering CAN
conversion logic, PGN aggregation and on-device capture.

## 1. Tests unitaires

Les tests Unity suivants couvrent chaque fonction de conversion Victron :

| Fichier | Description |
| --- | --- |
| `test_can_conversion.c` | Vérifie les conversions PGN 0x351, 0x355, 0x356, 0x35A, 0x35E, 0x35F, 0x371, 0x378, 0x379 et 0x382 pour des valeurs nominales et extrêmes. |
| `test_can_publisher_integration.c` | Valide l'intégration des registres Tiny simulés avec le tampon CAN. |

### Exécution

```sh
idf.py test
```

ou pour n'exécuter que les tests CAN :

```sh
idf.py test --target=unity -D TEST_FILTER="can_"
```

Les tests unitaires valident notamment :

- Les conversions tension/courant/température avec saturation INT16.
- L'agrégation énergie Wh via `can_publisher_conversion_reset_state()`.
- Les chaînes ASCII provenant des registres 0x01F4/0x01F6.
- Les limites CVL (charge/décharge) en présence ou absence de calcul `cvl_controller`.

## 2. Tests d’intégration

Le scénario `can_publisher_populates_buffer_for_all_channels` simule un jeu complet de
registres TinyBMS pour vérifier que `can_publisher_on_bms_update` :

1. Prépare le runtime CVL avant chaque diffusion.
2. Remplit le tampon circulaire pour tous les PGN configurés.
3. Conserve les timestamps TinyBMS (utilisés par le keepalive Victron).

Ce test fonctionne hors RTOS : aucun bus CAN physique n'est requis.

## 3. Validation matérielle (capture CAN)

Le script `tools/can_capture.sh` encapsule `candump` (can-utils) pour enregistrer les trames :

```sh
sudo ./tools/can_capture.sh can0 capture.log
```

- `-L` assure l'horodatage absolu pour vérifier les intervalles keepalive (~1 s sur PGN 0x351/0x355/0x356).
- Le fichier log peut être analysé avec `grep`/`awk` ou des scripts Python pour confirmer les PGN, DLC et valeurs.

### Critères d’acceptation terrain

- **Keepalive** : chaque PGN critique (0x351, 0x355, 0x356, 0x35A) doit apparaître toutes les 1 ±0.2 s.
- **PGN / DLC** : les identifiants 29 bits doivent correspondre à `conversion_table.c`.
- **CVL** : en forçant l'entrée CVL (ex. SOC élevé), `candump` doit refléter la tension cible / limites calculées.
- **Alarmes** : les bits d'alarmes (PGN 0x35A) changent lorsque l'on force les seuils (banc de test ou simulateur Tiny).

## 4. Résumé

1. Lancer `idf.py test` pour valider automatiquement les conversions.
2. Utiliser `tools/can_capture.sh` lors des essais terrain pour capturer les trames réelles.
3. Comparer les valeurs décodées avec les enregistrements TinyBMS (SOC, CVL, alarmes) et s'assurer du respect des critères ci-dessus.

## 5. Validation terrain obligatoire

Une validation terrain est requise avant toute diffusion OTA majeure afin de confirmer le comportement CAN/Victron dans un environnement complet.

### 5.1 Préparation
- **Fenêtre** : réaliser l'essai durant la semaine du 29/07/2024, une fois la stabilisation firmware (étape 1) et la campagne d'endurance (étape 2) clôturées.
- **Lieu** : site pilote Victron (atelier R&D, bâtiment B, baie d'essais n°3) disposant d'un banc Victron MultiPlus et d'une batterie LiFePO4 instrumentée.
- **Équipe** : 1 ingénieur Tests (lead), 1 ingénieur Firmware (support), 1 représentant Intégration Client.

### 5.2 Matériel requis
- 1 gateway TinyBMS Web Gateway flashée avec la build candidate.
- 1 interface CAN-to-USB (PeakCAN ou CANable) avec portable Linux.
- 1 alimentation stabilisée 12 V / 10 A pour le banc Victron.
- 1 tablette/PC pour accès UI web et monitoring MQTT.
- Outils de capture : `candump`, `mosquitto_sub`, scripts d'analyse `tools/`.

### 5.3 Check-list d'exécution
1. **Initialisation** : consigner le hash Git, la version ESP-IDF et l'ID de configuration TinyBMS utilisée.
2. **Connexion** : brancher la gateway sur le bus CAN Victron, alimenter, vérifier la séquence de boot (logs `wifi`, `can_victron`).
3. **Keepalive** : capturer 10 minutes de trafic CAN et vérifier le respect des critères §3 (PGN, périodicité, CVL).
4. **Alarmes** : forcer deux scénarios (surtension, surtempérature) et confirmer la propagation des alarmes dans les trames et l'UI web.
5. **Intégration Victron** : vérifier la mise à jour de l'état batterie dans le GX (SOC, courant, tension) et le déclenchement éventuel de relais.
6. **MQTT/Cloud** (si activé) : valider la remontée des mesures via le broker de test et comparer aux captures CAN.
7. **Clôture** : documenter anomalies/pistes dans le ticket QA, archiver logs CAN, captures écran UI, rapport Victron.

### 5.4 Critères de réussite terrain
- 0 erreur critique dans les logs firmware sur la durée de l'essai.
- Alignement ±2 % entre SOC Victron et SOC TinyBMS sur l'ensemble des mesures.
- Alarmes critiques relayées en moins de 5 s sur le GX et via MQTT.
- Rapport d'essai validé par QA et stocké dans l'espace partagé.
