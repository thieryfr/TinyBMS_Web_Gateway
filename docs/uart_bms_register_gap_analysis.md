# Audit des registres TinyBMS requis pour la passerelle UART

## 1. Registres attendus dans la matrice consolidée

### 1.1 Registres explicitement listés

| Registre | Nom (matrice) | Type |
| --- | --- | --- |
| 36 | Battery Pack Voltage | FLOAT |
| 38 | Battery Pack Current | FLOAT |
| 40 | Min Cell Voltage | UINT16 |
| 41 | Max Cell Voltage | UINT16 |
| 42 | External Temperature #1 | INT16 |
| 45 | State Of Health | UINT16 |
| 46 | State Of Charge | UINT32 |
| 48 | Internal Temperature | INT16 |
| 50 | Online Status | UINT16 |
| 52 | Cell Imbalance Alarm | UINT8 |
| 102 | Max Discharge Current | UINT16 |
| 103 | Max Charge Current | UINT16 |
| 113 | Pack Temperature Min/Max | INT8 |
| 306 | Battery Capacity | UINT16 |
| 500 | Manufacturer Name | String |
| 501 | Firmware Version | UINT32 |
| 502 | Battery Family | String |
| 504 | Serial Number (part 1) | String |
| 505 | Serial Number (part 2) | String |

> Ces entrées proviennent des onglets Excel/JSON de la matrice normalisée utilisée pour la cartographie Victron ⇄ TinyBMS.【F:docs/mapping_normalized.csv†L3-L65】

### 1.2 Registres requis par les champs calculés

Les champs d'alarmes et d'avertissements (par ex. Battery High Voltage Alarm, Battery High Temp Alarm, Battery High Current Alarm, etc.) combinent plusieurs registres TinyBMS :

- 36 (packVoltage) avec 315 (highVoltageCutoff) et 316 (lowVoltageCutoff) pour les alarmes/avertissements de tension.
- 113 (min/max temperature) et 319 (highTempCutoff) pour les alarmes température.
- 42 (externalTemp) et 319 (highTempChargeCutoff) pour les alarmes de charge en température.
- 38 (packCurrent) avec 317 (overCurrentCutoff) et 318 (overChargeCurrentCutoff) pour les alarmes/avertissements de courant.

Ces dépendances sont explicitement mentionnées dans la colonne `compute_inputs` de la matrice consolidée.【F:docs/mapping_normalized.csv†L14-L34】

## 2. Cartographie côté pile UART

### 2.1 Registres couverts intégralement

La table `g_uart_bms_registers` interroge et convertit la majorité des registres listés ci-dessus :

- 36/38/40/41/42/43/45/46/48/50/51/52/113 sont lus via les adresses 0x0024–0x0071 et alimentent les champs `uart_bms_live_data_t` (`pack_voltage_v`, `pack_current_a`, `min_cell_mv`, `max_cell_mv`, `average_temperature_c`, `mosfet_temperature_c`, `state_of_charge_pct`, etc.) ainsi que `TinyBMS_LiveData` (`voltage`, `current`, `min_cell_mv`, `max_cell_mv`, `temperature`, `pack_temp_min`, `pack_temp_max`, `online_status`, `balancing_bits`).【F:main/uart_bms/uart_bms_protocol.c†L5-L185】【F:main/uart_bms/uart_bms.h†L37-L70】【F:docs/shared_data.h†L33-L74】【F:main/uart_bms/uart_response_parser.cpp†L150-L384】
- 306, 315, 316, 317, 318 et 319 sont pris en charge via les adresses 0x0132–0x013F pour renseigner la capacité, les seuils de tension et de courant ainsi que la température de coupure dans les deux structures.【F:main/uart_bms/uart_bms_protocol.c†L186-L269】【F:main/uart_bms/uart_response_parser.cpp†L150-L384】

### 2.2 Registres partiellement propagés

Certains registres sont interrogés mais ne sont exposés que dans la structure legacy (`uart_bms_live_data_t`) ou restent sous forme brute :

- 32 (lifetime counter) : stocké uniquement dans `uptime_seconds` côté legacy, aucun champ partagé associé.【F:main/uart_bms/uart_bms_protocol.c†L5-L17】【F:main/uart_bms/uart_response_parser.cpp†L318-L340】
- 51 (Need Balancing) : valeur brute recopiée dans `warning_bits` sans équivalent dans `TinyBMS_LiveData` (champ `NeedBalancing` réservé).【F:main/uart_bms/uart_bms_protocol.c†L138-L149】【F:main/uart_bms/uart_response_parser.cpp†L150-L216】【F:docs/shared_data.h†L55-L69】
- 306 (Battery Capacity) et 307 (Series cell count) : mis à jour dans `uart_bms_live_data_t` mais pas dans `TinyBMS_LiveData`, empêchant une réutilisation directe côté CAN/API.【F:main/uart_bms/uart_bms_protocol.c†L186-L209】【F:main/uart_bms/uart_response_parser.cpp†L150-L216】
- 500/501/502 (versions HW/FW internes) : les mots 16 bits sont décodés dans `uart_bms_live_data_t` mais aucune propagation texte ou struct côté partagé, alors que la matrice attend des chaînes (Manufacturer, Firmware Version, Battery Family).【F:docs/mapping_normalized.csv†L42-L65】【F:main/uart_bms/uart_bms_protocol.c†L270-L305】【F:main/uart_bms/uart_response_parser.cpp†L190-L216】

### 2.3 Registres absents de la pile UART

Les registres suivants, pourtant requis par la matrice consolidée, ne figurent pas dans `g_uart_bms_registers` ni dans la boucle de polling :

- 102 / 103 — limites dynamiques de courant de charge/décharge (CCL/DCL).
- 504 / 505 — sérialisation du numéro de série en deux blocs ASCII.

Aucun identifiant 0x0066/0x0067 (Reg 102/103) ou 0x01F8–0x01F9 (Reg 504/505) n'est présent dans la table des registres polled (cf. liste d'adresses 0x0020–0x01F6).【F:main/uart_bms/uart_bms_protocol.c†L5-L315】

## 3. Écarts fonctionnels à combler

- **Limites CCL/DCL (Reg 103/102)** : nécessaires pour alimenter les algorithmes Victron (CCL/DCL). Ajouter des métadonnées UART, lire les registres 0x0066/0x0067, convertir en ampères (échelle 0.1 A) et exposer les valeurs dans `uart_bms_live_data_t` ainsi que `TinyBMS_LiveData.max_charge_current` / `.max_discharge_current`.【F:docs/mapping_normalized.csv†L3-L4】【F:docs/shared_data.h†L44-L60】
- **Numéro de série et informations fabricant (Reg 500/504/505)** : la matrice attend des chaînes ASCII alors que la pile UART ne récupère que les versions internes. Prévoir la lecture des blocs string et un stockage textuel (p. ex. dans `TinyRegisterSnapshot` ou via de nouveaux champs dédiés).【F:docs/mapping_normalized.csv†L42-L65】【F:main/uart_bms/uart_bms_protocol.c†L270-L305】
- **Capacité & configuration pack (Reg 306/307)** : exposer les valeurs vers `TinyBMS_LiveData` pour éviter les relectures via les snapshots bruts et faciliter le pont CAN/MQTT.【F:main/uart_bms/uart_bms_protocol.c†L186-L209】【F:main/uart_bms/uart_response_parser.cpp†L150-L216】【F:docs/shared_data.h†L33-L74】
- **Need Balancing / Lifetime Counter** : décider si ces registres doivent être remontés à l'API publique (nouveaux champs ou événements), ou si l'on supprime le polling pour éviter de véhiculer des données inutilisées.

## 4. Préparation des évolutions dans la pile UART

1. Étendre `uart_bms_register_id_t` et `g_uart_bms_registers` avec les registres manquants (102, 103, 504, 505) et ajuster `UART_BMS_REGISTER_WORD_COUNT`/`g_uart_bms_poll_addresses` en conséquence.
2. Compléter `uart_response_parser.cpp` pour convertir ces nouveaux registres, remplir les structures legacy et partagée, et propager les chaînes via `TinyRegisterSnapshot`.
3. Ajouter les champs manquants dans `TinyBMS_LiveData` (ou exposer via snapshots dédiés) pour la capacité, les versions et le numéro de série, puis consommer ces données côté modules CAN/MQTT.
4. Documenter les nouveaux champs dans la configuration web/API et mettre à jour la matrice de tests.
