# PGN TinyBMS ↔ Victron

Ce document détaille le mapping entre les mesures TinyBMS et les PGN attendus par un chargeur/monitoring Victron. Toutes les conversions sont centralisées dans `main/can_publisher/conversion_table.c` et vérifiées par `test/test_can_conversion.c`.

## Notation
- **Source TinyBMS** : champ de `uart_bms_live_data_t` (ex. `pack_voltage_v`) ou registre TinyBMS.
- **Échelle CAN** : facteur appliqué avant sérialisation (voir `VICTRON_ENCODE_*` dans `conversion_table.c`).
- **Clamping** : saturation appliquée avant encodage.

## PGN 0x351 — Charge Voltage / Current Limits (CVL/CCL/DCL)
- **CVL** : privilégie le résultat du contrôleur `cvl_controller` (`can_publisher_cvl_get_latest`). À défaut, reprend `pack_voltage_v` ou la consigne `overvoltage_cutoff_mv` si disponible.
  - Conversion : volts → entier non signé avec facteur ×100 (0,01 V).【F:main/can_publisher/conversion_table.c†L332-L384】【F:main/can_publisher/cvl_controller.c†L167-L201】
  - Clamp : `[0, 655.35] V`.
- **CCL** : `charge_overcurrent_limit_a` avec repli sur `peak_discharge_current_limit_a` lorsque la limite de charge est absente.
  - Conversion : ampères → entier non signé ×10 (0,1 A).【F:main/can_publisher/conversion_table.c†L332-L384】
- **DCL** : `discharge_overcurrent_limit_a` avec repli sur `peak_discharge_current_limit_a`.
  - Conversion : ampères → entier non signé ×10 (0,1 A).【F:main/can_publisher/conversion_table.c†L332-L384】

## PGN 0x355 — State of Charge / Health (SOC/SOH)
- **SOC** : `live->state_of_charge_pct` (0–100 %).
  - Conversion : pourcentage → entier non signé ×10 (0,1 %).【F:main/can_publisher/conversion_table.c†L500-L552】
- **SOH** : `live->state_of_health_pct` avec défaut à 100 %.
  - Conversion : pourcentage → entier non signé ×10 (0,1 %).【F:main/can_publisher/conversion_table.c†L553-L597】

## PGN 0x356 — Battery Voltage / Current / Temperature
- **Pack voltage** : `live->pack_voltage_v`.
  - Conversion : volts → entier non signé ×100 (0,01 V).【F:main/can_publisher/conversion_table.c†L415-L424】
- **Pack current** : `live->pack_current_a`.
  - Conversion : ampères → entier signé ×10 (0,1 A).【F:main/can_publisher/conversion_table.c†L415-L424】
- **MOSFET temperature** : `live->mosfet_temperature_c` (représente la température interne TinyBMS utilisée par Victron).
  - Conversion : degrés Celsius → entier signé ×10 (0,1 °C).【F:main/can_publisher/conversion_table.c†L415-L424】

## PGN 0x35A — Alarmes & avertissements
Chaque champ 2 bits encode un niveau Victron (0 = OK, 1 = warning, 2 = alarm, 3 = réservé). Les huit octets couvrent les alarmes
et avertissements attendus par le protocole CAN-BMS. Les bits réservés sont explicitement positionnés à `0b11` pour signaler
leur absence. L’ensemble des seuils s’appuie sur les registres TinyBMS (`*_cutoff`, surintensités) ou les températures internes/
externes calculées par le pont.【F:main/can_publisher/conversion_table.c†L115-L206】【F:main/can_publisher/conversion_table.c†L434-L547】

| Byte | Bits | Champ | Source et seuils |
| ---- | ---- | ----- | ---------------- |
| 0 | 0-1 | General Alarm | Passe à 2 dès qu’une alarme critique est active, sinon 0. |
| 0 | 2-3 | Battery High Voltage Alarm | `pack_voltage_v` vs `overvoltage_cutoff_mv` (alarm ≥ seuil, warning ≥95 %). |
| 0 | 4-5 | Battery Low Voltage Alarm | `pack_voltage_v` vs `undervoltage_cutoff_mv` (alarm ≤ seuil, warning ≤105 %). |
| 0 | 6-7 | Battery High Temperature Alarm | `max(mosfet_temperature_c, pack_temperature_max_c)` vs `overheat_cutoff_c` (warning à 90 %). |
| 1 | 0-1 | Battery Low Temperature Alarm | `min(mosfet_temperature_c, pack_temperature_min_c)` (alarm < −10 °C, warning < 0 °C). |
| 1 | 2-3 | Battery High Temp Charge Alarm | Température externe `auxiliary_temperature_c` vs `overheat_cutoff_c` (warning à 90 %). |
| 1 | 4-5 | Battery Low Temp Charge Alarm | Réservé : forcé à `0b11` (non fourni par TinyBMS). |
| 1 | 6-7 | Battery High Current Alarm | Courant de décharge `|-pack_current_a|` vs `discharge_overcurrent_limit_a` (warning ≥80 %). |
| 2 | 0-1 | Battery High Charge Current Alarm | Courant de charge `pack_current_a` vs `charge_overcurrent_limit_a` (warning ≥80 %). |
| 2 | 2-7 | Contactor/short/BMS Internal Alarms | Réservés (`0b11`). |
| 3 | 0-1 | Cell Imbalance Alarm | `max_cell_mv - min_cell_mv` (alarm ≥80 mV, warning ≥40 mV). |
| 3 | 2-7 | Reserved | `0b11`. |
| 4 | 0-1 | General Warning | 1 lorsqu’au moins un warning est actif (2 si une alarme est présente). |
| 4 | 2-3 | Battery High Voltage Warning | Même seuils que l’alarme (95 %). |
| 4 | 4-5 | Battery Low Voltage Warning | Même seuils que l’alarme (105 %). |
| 4 | 6-7 | Battery High Temperature Warning | Avertissement à 90 % de `overheat_cutoff_c`. |
| 5 | 0-1 | Battery Low Temperature Warning | Identique au champ d’alarme (0 °C / −10 °C). |
| 5 | 2-3 | Battery High Temp Charge Warning | Température externe > 90 % de `overheat_cutoff_c`. |
| 5 | 4-5 | Battery Low Temp Charge Warning | `auxiliary_temperature_c` < `low_temp_charge_cutoff_c` (alarm), warning 5 °C au-dessus. |
| 5 | 6-7 | Battery High Current Warning | Décharge ≥80 % de `discharge_overcurrent_limit_a`. |
| 6 | 0-1 | Battery High Charge Current Warning | Charge ≥80 % de `charge_overcurrent_limit_a`. |
| 6 | 2-7 | Reserved | `0b11`. |
| 7 | 0-1 | Cell Imbalance Warning | Reflète le niveau (0/1/2) du déséquilibre cellulaire. |
| 7 | 2-3 | System Status | Non exposé par TinyBMS → `0b11`. |
| 7 | 4-7 | Reserved | `0b11`. |

Les seuils « charge basse température » utilisent désormais le registre TinyBMS 0x0140 (`low_temp_charge_cutoff_c`), ajouté à la
trame UART pour récupérer la consigne de coupure de charge basse température.【F:main/uart_bms/uart_bms_protocol.c†L229-L260】【F:main/uart_bms/uart_response_parser.cpp†L234-L255】

## PGN 0x35E / 0x371 — Informations fabricant & nom
- **0x35E Manufacturer** : chaîne `CONFIG_TINYBMS_CAN_MANUFACTURER` tronquée/padée à 8 caractères (ou valeur lue sur les registres 0x01F4/0x01F5 si disponibles).【F:main/can_publisher/conversion_table.c†L708-L718】
- **0x371 Name part 2** : suite du nom batterie (`CONFIG_TINYBMS_CAN_BATTERY_NAME` ou registres 0x01F6/0x01F7) encodée sur 8 octets.【F:main/can_publisher/conversion_table.c†L719-L725】

## PGN 0x35F — Identification TinyBMS
- **Octets 0-1 — Model ID** : combinaison `hardware_version` (LSB) / `hardware_changes_version` (MSB) issue du registre 0x01F4.【F:main/can_publisher/conversion_table.c†L189-L216】【F:main/uart_bms/uart_bms.h†L58-L63】
- **Octets 2-3 — Firmware public & flags** : registre 0x01F5 (firmware public en LSB, indicateurs en MSB).【F:main/can_publisher/conversion_table.c†L217-L241】
- **Octets 4-5 — Capacité en ligne** : registre 0x0132 (0,01 Ah) conservé brut pour reporter la capacité mesurée par TinyBMS.【F:main/can_publisher/conversion_table.c†L242-L249】
- **Octets 6-7 — Firmware interne** : registre 0x01F6 (version interne 16 bits).【F:main/can_publisher/conversion_table.c†L250-L258】

Les valeurs sont mises à zéro si TinyBMS ne fournit pas les registres ; les champs sont saturés à 0xFFFF en cas de débordement.

## PGN 0x378 — Energy Counters
- **Charge Wh** : accumulateur interne `s_energy_charged_wh` (double) mis à jour à chaque appel via `update_energy_counters()`.
- **Discharge Wh** : accumulateur `s_energy_discharged_wh`.
- Les deux compteurs sont encodés sur 32 bits Little Endian après division par 100 (résolution 0,1 kWh).【F:main/can_publisher/conversion_table.c†L225-L287】【F:main/can_publisher/conversion_table.c†L549-L569】
- Les valeurs sont recalculées à chaque redémarrage ; prévoir une persistance NVS si nécessaire (non implémenté).

## PGN 0x379 — Installed Capacity
- Basé sur `live->battery_capacity_ah` avec repli sur `series_cell_count × 2.5` Ah si la valeur TinyBMS est absente.
- Ajusté par `state_of_health_pct` (capacité réduite proportionnellement).
- Conversion : Ah → entier non signé ×1 (1 Ah).【F:main/can_publisher/conversion_table.c†L574-L599】

## PGN 0x382 — Battery Family
- Chaîne ASCII (8 caractères) lue depuis les registres 0x01F8–0x01FF ; repli sur `CONFIG_TINYBMS_CAN_BATTERY_FAMILY` si vide ou non renseignée.【F:main/can_publisher/conversion_table.c†L726-L738】
- Utilisé par Victron pour regrouper les profils de charge.

## Validation
- `test/test_can_conversion.c` couvre les cas nominaux et extrêmes pour chaque PGN.
- `docs/testing/validation_plan.md` décrit les captures CAN à réaliser sur banc Victron.
- Toute modification du mapping nécessite :
  1. Mise à jour de cette documentation.
  2. Ajustement de `docs/pgn_mapping.xlsx`.
  3. Exécution des tests unitaires `idf.py test` et validation CAN réelle.

