# Audit des registres TinyBMS requis pour la passerelle UART

## 1. Registres attendus dans la matrice consolidée

Les documents fournis identifient 19 registres TinyBMS nécessaires à la cartographie CAN.【F:archive/docs/mapping_audit.md†L14-L36】 Les principales entrées sont rappelées ci-dessous.

| Registre | Nom (matrice JSON) | Type |
| --- | --- | --- |
| 36 | Battery Pack Voltage | FLOAT |
| 38 | Battery Pack Current | FLOAT |
| 40 | Min Cell Voltage | UINT16 |
| 41 | Max Cell Voltage | UINT16 |
| 42 | External Temperature #1 | INT16 |
| 45 | State Of Health | UINT16 |
| 46 | State Of Charge | UINT32 |
| 48 | Internal Temperature | INT16 |
| 50 | System Status | UINT16 |
| 52 | Cell Imbalance Alarm | UINT8 |
| 102 | Max Discharge Current | UINT16 |
| 103 | Max Charge Current | UINT16 |
| 113 | Pack Temperature Min/Max | INT8 pair |
| 306 | Battery Capacity | UINT16 |
| 315–320 | Cut-off thresholds | UINT16/INT16 |
| 500–502 | Manufacturer / firmware words | UINT16 |
| 504–505 | Serial Number (ASCII) | String |

> Source : `docs/TinyBMS_CAN_BMS_mapping.json` et audit automatisé.【F:docs/TinyBMS_CAN_BMS_mapping.json†L5-L613】【F:archive/docs/mapping_audit.md†L14-L36】

### 1.2 Registres requis par les champs calculés

Les champs d'alarmes et d'avertissements (par ex. Battery High Voltage Alarm, Battery High Temp Alarm, Battery High Current Alarm, etc.) combinent plusieurs registres TinyBMS :

- 36 (packVoltage) avec 315 (highVoltageCutoff) et 316 (lowVoltageCutoff) pour les alarmes/avertissements de tension.
- 113 (min/max temperature) et 319 (highTempCutoff) pour les alarmes température.
- 42 (externalTemp) et 319 (highTempChargeCutoff) pour les alarmes de charge en température.
- 38 (packCurrent) avec 317 (overCurrentCutoff) et 318 (overChargeCurrentCutoff) pour les alarmes/avertissements de courant.

Ces dépendances sont explicitement mentionnées dans la colonne `compute_inputs` de la matrice consolidée.【F:archive/docs/mapping_normalized.csv†L14-L34】

## 2. Cartographie côté pile UART

### 2.1 Registres couverts intégralement

La table `g_uart_bms_registers` interroge et convertit la majorité des registres listés ci-dessus :

- 36/38/40/41/42/45/46/48/50/52/113 sont lus via les adresses 0x0024–0x0071 et alimentent les champs `uart_bms_live_data_t` (`pack_voltage_v`, `pack_current_a`, `min_cell_mv`, `max_cell_mv`, `average_temperature_c`, `mosfet_temperature_c`, `state_of_charge_pct`, etc.) ainsi que `TinyBMS_LiveData` (`voltage`, `current`, `min_cell_mv`, `max_cell_mv`, `temperature`, `pack_temp_min`, `pack_temp_max`, `online_status`, `balancing_bits`).【F:main/uart_bms/uart_bms_protocol.c†L5-L185】【F:main/uart_bms/uart_bms.h†L37-L70】【F:docs/shared_data.h†L33-L74】【F:main/uart_bms/uart_response_parser.cpp†L150-L340】
- 306, 315, 316, 317, 318 et 319 sont pris en charge via les adresses 0x0132–0x013F pour renseigner la capacité, les seuils de tension et de courant ainsi que la température de coupure dans les deux structures.【F:main/uart_bms/uart_bms_protocol.c†L186-L269】【F:main/uart_bms/uart_response_parser.cpp†L150-L306】

### 2.2 Registres partiellement propagés

Certains registres sont interrogés mais ne sont exposés que dans la structure legacy (`uart_bms_live_data_t`) ou restent sous forme brute :

- 306 (Battery Capacity) : disponible mais non recopié dans `TinyBMS_LiveData`, ce qui impose un accès aux snapshots bruts côté CAN/API.【F:main/uart_bms/uart_response_parser.cpp†L150-L238】
- 500/501/502 : les mots 16 bits sont exposés mais les chaînes ASCII associées restent à extraire via `decode_ascii_from_registers()` pour alimenter les trames 0x380/0x381/0x35E lorsque l’on souhaite s’affranchir des constantes de configuration.【F:main/can_publisher/conversion_table.c†L706-L738】
- 504/505 : la fenêtre 0x01F8–0x01FF est interrogée mais les mots ne sont pas décodés en ASCII ; aucune structure ne conserve le numéro de série complet.【F:archive/docs/mapping_audit.md†L32-L36】

### 2.3 Registres absents de la pile UART

- **102 / 103** — limites dynamiques de courant de charge/décharge : absents de `g_uart_bms_registers` et de `TinyBMS_LiveData`. Leur ajout est indispensable pour publier CCL/DCL selon la matrice Victron.【F:archive/docs/mapping_audit.md†L14-L36】
- Les autres registres répertoriés par la matrice sont soit couverts, soit déjà polled en brut.

## 3. Écarts fonctionnels à combler

- **Limites CCL/DCL (Reg 103/102)** : nécessaires pour alimenter les algorithmes Victron (CCL/DCL). Ajouter des métadonnées UART, lire les registres 0x0066/0x0067, convertir en ampères (échelle 0,1 A) et exposer les valeurs dans `uart_bms_live_data_t` ainsi que `TinyBMS_LiveData.max_charge_current` / `.max_discharge_current`.【F:docs/TinyBMS_CAN_BMS_mapping.json†L24-L57】【F:docs/shared_data.h†L44-L69】
- **Numéro de série (Reg 504/505)** : décoder et stocker les blocs ASCII pour préparer les trames CAN 0x380/0x381 et l’API Web. Réutiliser `decode_ascii_from_registers()` côté UART afin d’éviter un double parsing côté CAN.【F:archive/docs/mapping_audit.md†L32-L36】【F:main/can_publisher/conversion_table.c†L706-L738】
- **Capacité (Reg 306)** : propager la valeur dans `TinyBMS_LiveData` pour permettre un encodage direct des trames 0x35F/0x379 sans relecture brute des registres.【F:main/uart_bms/uart_response_parser.cpp†L150-L238】

## 4. Préparation des évolutions dans la pile UART

1. Étendre `uart_bms_register_id_t` et `g_uart_bms_registers` avec les registres manquants (102, 103) et documenter le décodage ASCII 504/505.
2. Compléter `uart_response_parser.cpp` pour mettre à jour les structures `uart_bms_live_data_t` et `TinyBMS_LiveData`, puis alimenter `TinyRegisterSnapshot` pour les usages diagnostics.
3. Ajouter les champs manquants dans `TinyBMS_LiveData` (courants max, capacité, numéro de série) pour les rendre accessibles aux modules CAN/MQTT.
4. Mettre à jour la documentation (présent fichier, `archive/docs/can_mapping_state.md`, `archive/docs/pgn_conversions.md`) et étendre la matrice de tests UART/CAN.
