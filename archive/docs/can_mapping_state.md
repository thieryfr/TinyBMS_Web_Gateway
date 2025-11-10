# État du mapping TinyBMS ↔ Victron CAN

Ce document consolide les résultats de l'audit automatique (`tools/audit_mapping.py`) et des vérifications manuelles effectuées dans le firmware. Il sert de référence pour suivre l'intégration des nouveaux fichiers de mapping (`docs/UART_CAN_mapping.json`, `docs/TinyBMS_CAN_BMS_mapping.json`).

## 1. Synthèse de l’audit

- 67 champs CAN décrits par les documents sources (19 registres TinyBMS, 21 CAN ID).【F:archive/docs/mapping_audit.md†L5-L38】
- L’ensemble des registres requis (dont 102/103 pour les limites dynamiques) est désormais interrogé et décodé côté UART.【F:main/uart_bms/uart_bms_protocol.c†L5-L358】
- Deux CAN ID Victron restent en attente (0x305 keepalive, 0x307 identifiant onduleur) ; les autres trames de la matrice sont publiées par `can_publisher`.【F:main/can_publisher/conversion_table.c†L738-L816】
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
| 0x370 | Nom batterie partie 1 | `encode_battery_name_part1()` publie les 8 premiers caractères (ASCII). | ✅ Aligné【F:main/can_publisher/conversion_table.c†L640-L747】 |
| 0x372 | Comptage modules (OK / block charge / block discharge / offline) | `encode_module_status_counts()` dérive les compteurs à partir des limites dynamiques et bits d’alarme. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L652-L689】 |
| 0x373 | Min/Max cell V & température | `encode_cell_voltage_temperature_extremes()` encode les valeurs en mV et Kelvin. | ✅ Aligné【F:main/can_publisher/conversion_table.c†L691-L712】 |
| 0x374–0x377 | Identifiants de cellule/température extrêmes | Chaînes synthétiques `MINVxxxx` / `MAXVxxxx` / `MINT±xxx` / `MAXT±xxx`. | ✅ Publie des identifiants lisibles【F:main/can_publisher/conversion_table.c†L714-L747】 |
| 0x380–0x381 | Numéro de série (ASCII) | `encode_serial_number_part1/part2()` lit 0x01FA+ et replie sur la configuration si vide. | ✅ Aligné (fallback configuré)【F:main/can_publisher/conversion_table.c†L749-L768】 |

## 3. Couverture des registres UART

| Registre TinyBMS | Usage CAN documenté | Implémentation actuelle | Statut |
| --- | --- | --- | --- |
| 36 / 38 / 40 / 41 / 42 / 45 / 46 / 48 / 50 / 52 | Mesures pack, SOC/SOH, alarmes | Récupérés et propagés vers `uart_bms_live_data_t` + `TinyBMS_LiveData`. | ✅ | 
| 102 / 103 | CCL/DCL dynamiques | Métadonnées UART + propagation vers live data (`max_*_current_limit_a`). | ✅ |
| 113 | Températures min/max | Convertis (INT8 pair). | ✅ | 
| 306 | Capacité | Disponible (0,01 Ah) mais non exposé dans `TinyBMS_LiveData`. | ⚠️ Propager vers structure partagée【F:main/uart_bms/uart_response_parser.cpp†L150-L238】 |
| 315–320 | Seuils tension/courant/température | Lus et utilisés par l’encodeur d’alarmes. | ✅ | 
| 500–502 | Infos fabricant/FW | Lus et exposés (numériques) + chaînes ASCII selon besoin. | ✅ | 
| 504–505 | Numéro de série | Décodage ASCII à la volée pour les PGN 0x380/0x381 (fallback configuration). | ✅ |

## 4. Plan d’intégration

1. **Finaliser les trames manquantes**
   - Implémenter (ou laisser au module keepalive) la trame 0x305 et l’identifiant 0x307 selon les spécifications Victron.
   - Vérifier l’attendu côté Victron pour 0x307 (valeurs fixes + signature ASCII) et ajouter les tests correspondants.

2. **Validation système**
   - Capturer sur banc les nouveaux PGN pour confirmer la compatibilité Victron (0x370, 0x372–0x377, 0x380–0x381).
   - Couvrir ces trames via l’outillage `tools/audit_mapping.py` afin que les régressions soient détectées automatiquement.

3. **Documentation**
   - Mettre à jour les matrices Excel/JSON et la documentation utilisateur (PGN détaillés) pour refléter les nouveaux encodages.

Ce plan cible désormais uniquement les éléments encore ouverts (keepalive / identifiant onduleur et validation système).
