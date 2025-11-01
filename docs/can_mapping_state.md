# État du mapping TinyBMS ↔ Victron CAN

Ce document consolide les résultats de l’audit automatique (`tools/audit_mapping.py`) et des vérifications manuelles effectuées dans le firmware. Il sert de référence pour suivre l’intégration des nouveaux fichiers de mapping (`docs/UART_CAN_mapping.xlsx`, `docs/TinyBMS_CAN_BMS_mapping.json`).

## 1. Synthèse de l’audit

- 67 champs CAN décrits par les documents sources (19 registres TinyBMS, 21 CAN ID).【F:docs/mapping_audit.md†L5-L38】
- Le firmware lit déjà 17/19 registres requis ; les registres 102 et 103 restent absents de la pile UART.【F:docs/mapping_audit.md†L14-L36】
- 10 CAN ID Victron ne sont pas publiés actuellement (0x305, 0x307, 0x370, 0x372–0x377, 0x380, 0x381).【F:docs/mapping_audit.md†L38-L62】
- Les conversions CVL/CCL/DCL et SOC/SOH sont alignées avec les documents JSON (0,1 V et 1 %).【F:main/can_publisher/conversion_table.c†L444-L528】【F:docs/TinyBMS_CAN_BMS_mapping.json†L5-L86】

## 2. Couverture actuelle par CAN ID

| CAN ID | Champs documentés | Implémentation actuelle | Statut |
| --- | --- | --- | --- |
| 0x351 | CVL/CCL/DCL (+ DVL inactif) | `encode_charge_limits()` encode CVL/CCL/DCL en 0,1 V / 0,1 A, conserve la logique CVL existante. | ✅ Aligné (champ DVL non utilisé)【F:main/can_publisher/conversion_table.c†L444-L486】 |
| 0x355 | SOC/SOH + SOC hi-res | `encode_soc_soh()` publie SOC/SOH en pas de 1 %, ajoute SOC hi-res lorsque disponible. | ✅ Aligné (résolution 1 %)【F:main/can_publisher/conversion_table.c†L491-L528】 |
| 0x356 | Voltage / courant / température | `encode_voltage_current_temperature()` (0,01 V / 0,1 A / 0,1 °C). | ✅ Aligné【F:main/can_publisher/conversion_table.c†L530-L551】 |
| 0x35A | Alarmes & warnings | `encode_alarm_status()` gère l’intégralité des bits attendus. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L553-L678】 |
| 0x35E | Manufacturer name | Lecture ASCII 0x01F4/0x01F5 avec repli configuration. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L706-L718】 |
| 0x35F | Identité batterie (HW/FW/capacité) | `encode_battery_identification()` conforme à la matrice. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L240-L343】 |
| 0x371 | Battery/BMS name part 2 | Lecture ASCII 0x01F6/0x01F7. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L719-L725】 |
| 0x378 | Compteurs d’énergie | `encode_energy_counters()` (Wh/100). | ✅ Aligné【F:main/can_publisher/conversion_table.c†L706-L738】 |
| 0x379 | Capacité installée | `encode_installed_capacity()` (Ah ×1, ajusté par SOH). | ✅ Aligné【F:main/can_publisher/conversion_table.c†L731-L757】 |
| 0x382 | Battery family name | Lecture ASCII 0x01F8–0x01FF. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L726-L738】 |
| 0x305 | Keepalive | Aucun encodeur. | ❌ À implémenter selon matrice【F:docs/TinyBMS_CAN_BMS_mapping.json†L184-L207】 |
| 0x307 | Identifiant onduleur / signature « VIC » | Non publié. | ❌ À implémenter【F:docs/TinyBMS_CAN_BMS_mapping.json†L208-L245】 |
| 0x370 | Nom batterie partie 1 | Non publié (partie 2 uniquement). | ❌ À implémenter【F:docs/TinyBMS_CAN_BMS_mapping.json†L374-L397】 |
| 0x372 | Comptage modules (OK / block charge / block discharge / offline) | Non publié. | ❌ À implémenter【F:docs/TinyBMS_CAN_BMS_mapping.json†L398-L439】 |
| 0x373 | Min/Max cell V & température | Non publié. | ❌ À implémenter【F:docs/TinyBMS_CAN_BMS_mapping.json†L440-L487】 |
| 0x374–0x377 | Identifiants de cellule/température extrêmes | Non publiés. | ❌ À implémenter (chaînes ASCII)【F:docs/TinyBMS_CAN_BMS_mapping.json†L488-L567】 |
| 0x380–0x381 | Numéro de série (ASCII) | Non publiés (données non décodées côté UART). | ❌ Bloqué par lecture UART【F:docs/TinyBMS_CAN_BMS_mapping.json†L568-L613】 |

## 3. Couverture des registres UART

| Registre TinyBMS | Usage CAN documenté | Implémentation actuelle | Statut |
| --- | --- | --- | --- |
| 36 / 38 / 40 / 41 / 42 / 45 / 46 / 48 / 50 / 52 | Mesures pack, SOC/SOH, alarmes | Récupérés et propagés vers `uart_bms_live_data_t` + `TinyBMS_LiveData`. | ✅ | 
| 102 / 103 | CCL/DCL dynamiques | Non présents dans `g_uart_bms_registers`, aucune lecture UART. | ❌ Ajouter métadonnées + décodage【F:docs/mapping_audit.md†L14-L36】 |
| 113 | Températures min/max | Convertis (INT8 pair). | ✅ | 
| 306 | Capacité | Disponible (0,01 Ah) mais non exposé dans `TinyBMS_LiveData`. | ⚠️ Propager vers structure partagée【F:main/uart_bms/uart_response_parser.cpp†L150-L238】 |
| 315–320 | Seuils tension/courant/température | Lus et utilisés par l’encodeur d’alarmes. | ✅ | 
| 500–502 | Infos fabricant/FW | Lus et exposés (numériques) + chaînes ASCII selon besoin. | ✅ | 
| 504–505 | Numéro de série | Polled mais pas décodés ; aucune propagation ASCII. | ⚠️ Ajouter décodage et stockage【F:docs/mapping_audit.md†L32-L36】 |

## 4. Plan d’intégration

1. **Étendre la pile UART**
   - Ajouter les registres 102/103 dans `uart_bms_protocol.c` et `uart_response_parser.cpp` pour fournir les limites dynamiques CCL/DCL nécessaires aux trames 0x351 et 0x35A. Prévoir la propagation dans `TinyBMS_LiveData` (ampères et dixième d’ampère).【F:docs/mapping_audit.md†L14-L36】
   - Décoder les blocs ASCII 504/505 pour exposer le numéro de série et préparer l’encodeur CAN 0x380/0x381. S’appuyer sur `decode_ascii_from_registers()` existant.【F:main/can_publisher/conversion_table.c†L660-L738】
   - Vérifier la mise à disposition des compteurs modules (registres à identifier via TinyBMS doc) avant d’implémenter 0x372–0x377.

2. **Compléter le CAN publisher**
   - Ajouter des canaux pour 0x305, 0x307, 0x370 et 0x372–0x377, 0x380–0x381 en respectant les échelles des documents sources. Réutiliser les helpers existants (`encode_ascii_field`, `encode_u16_scaled`).【F:main/can_publisher/conversion_table.c†L706-L738】
   - S’assurer que la logique CVL existante reste inchangée (pas de modification `cvl_controller`).
   - Introduire des tests unitaires pour chaque nouvelle trame (`test/test_can_conversion.c`).

3. **Documentation & validation**
   - Regénérer `docs/mapping_audit.md` après chaque incrément pour suivre la couverture. Mettre à jour `docs/pgn_conversions.md` et `README.md` une fois l’intégration terminée.【F:docs/pgn_conversions.md†L1-L86】【F:README.md†L57-L66】
   - Actualiser la matrice Excel/JSON si des ajustements sont nécessaires (conserver l’échelle CVL 0,1 V pour rester cohérent avec l’implémentation).
   - Plan de tests :
     1. Simulation UART pour valider la lecture des nouveaux registres.
     2. Capture CAN (via analyseur) pour chaque nouveau PGN.
     3. Validation Victron (chargeur + Cerbo) pour confirmer l’interopérabilité.

4. **Suivi & déploiement**
   - Créer une checklist de livraison (UART → CAN → tests) et consigner les jalons dans le suivi projet.
   - Prévoir une itération OTA avec logs CAN renforcés afin de confirmer la bonne publication des trames ajoutées.

Ce plan garantit une intégration progressive des données documentées sans modifier la logique de calcul du CVL existante.
