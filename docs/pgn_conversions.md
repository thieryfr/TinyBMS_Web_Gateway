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

## PGN 0x35A — Alarms
- Chaque alarme est encodée sur 2 bits (0=OK, 1=Avertissement, 2=Critique) dans les octets 0 et 1 :
  - **Undervoltage** : comparaison `pack_voltage_v` vs `undervoltage_cutoff_mv` (critique si ≤ seuil, avertissement si ≤ seuil ×1,05).
  - **Overvoltage** : `pack_voltage_v` vs `overvoltage_cutoff_mv` (critique si ≥ seuil, avertissement si ≥ seuil ×0,95).
  - **Haute température** : `max(mosfet_temperature_c, pack_temperature_max_c)` vs `overheat_cutoff_c` (critique si dépassé, avertissement à 90 %).
  - **Basse température** : `min(mosfet_temperature_c, pack_temperature_min_c)` (critique < −10 °C, avertissement < 0 °C).
  - **Équilibrage / déséquilibre cellules** : `max_cell_mv - min_cell_mv` (≥80 mV critique, ≥40 mV avertissement).
  - **SOC bas** : `state_of_charge_pct` (≤5 % critique, ≤15 % avertissement).
  - **SOC élevé** : `state_of_charge_pct ≥ 98 %` et `pack_current_a > 1 A`.
- L'octet 7 reflète le niveau d'alarme le plus élevé (0=OK, 1=Avertissement, 2=Critique).【F:main/can_publisher/conversion_table.c†L429-L520】

## PGN 0x35E / 0x35F / 0x371 — Informations fabricant & nom
- **0x35E Manufacturer** : chaîne `CONFIG_TINYBMS_CAN_MANUFACTURER` tronquée/padée à 8 caractères.【F:main/can_publisher/conversion_table.c†L299-L335】
- **0x35F Battery info** : `CONFIG_TINYBMS_CAN_BATTERY_NAME` (8 caractères max).【F:main/can_publisher/conversion_table.c†L336-L371】
- **0x371 Name part 2** : suite du nom ou informations libres (8 caractères).【F:main/can_publisher/conversion_table.c†L372-L408】

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
- Encodage ASCII (8 caractères) de `CONFIG_TINYBMS_CAN_BATTERY_FAMILY`.
- Utilisé par Victron pour regrouper les profils de charge.【F:main/can_publisher/conversion_table.c†L636-L702】

## Validation
- `test/test_can_conversion.c` couvre les cas nominaux et extrêmes pour chaque PGN.
- `docs/testing/validation_plan.md` décrit les captures CAN à réaliser sur banc Victron.
- Toute modification du mapping nécessite :
  1. Mise à jour de cette documentation.
  2. Ajustement de `docs/pgn_mapping.xlsx`.
  3. Exécution des tests unitaires `idf.py test` et validation CAN réelle.

