# PGN 0x35A – Alarmes et avertissements TinyBMS vers Victron GX

Ce document détaille la correspondance des bits envoyés dans la trame CAN 0x35A ainsi que les registres TinyBMS utilisés pour déterminer chaque état d'alarme ou de warning.

## Octets d'alarme (0x35A octets 0 à 3)

| Octet | Bits | Champ Victron | Condition évaluée | Registres TinyBMS |
| --- | --- | --- | --- | --- |
| 0 | 0-1 | General Alarm | Passe à `10b` si l'une des autres alarmes est au niveau 2, sinon `00b`. | Repose sur les mêmes mesures que les champs détaillés ci-dessous. |
| 0 | 2-3 | Battery High Voltage Alarm | `pack_voltage_v ≥ overvoltage_cutoff_v`. | 0x0024 (tension pack), 0x013B (coupure surtension). |
| 0 | 4-5 | Battery Low Voltage Alarm | `pack_voltage_v ≤ undervoltage_cutoff_v`. | 0x0024, 0x013C (coupure sous-tension). |
| 0 | 6-7 | Battery High Temp Alarm | `max(mosfet_temp, pack_max_temp) ≥ overheat_cutoff_c`. | 0x0030 (MOSFET), 0x0071 (pack min/max), 0x013F (seuil surchauffe). |
| 1 | 0-1 | Battery Low Temp Alarm | `min(mosfet_temp, pack_min_temp) ≤ -10 °C` (seuil fixe). | 0x0030, 0x0071. |
| 1 | 2-3 | Charge High Temp Alarm | `aux_temp ≥ overheat_cutoff_c`. | 0x002B (température auxiliaire), 0x013F. |
| 1 | 4-5 | Reserved | Forcé à `11b`. | — |
| 1 | 6-7 | Discharge Over-Current Alarm | `|-pack_current_a| ≥ discharge_overcurrent_limit_a`. | 0x0026 (courant pack), 0x013D (limite décharge). |
| 2 | 0-1 | Charge Over-Current Alarm | `pack_current_a ≥ charge_overcurrent_limit_a`. | 0x0026, 0x013E (limite charge). |
| 2 | 2-7 | Reserved | Trois couples forcés à `11b`. | — |
| 3 | 0-1 | Cell Imbalance Alarm | Δcellules ≥ 80 mV (niveau 2) ou ≥ 40 mV (niveau 1). | 0x0028 (cellule min), 0x0029 (cellule max). |
| 3 | 2-7 | Reserved | Champs forcés à `11b`. | — |

## Octets d'avertissement (0x35A octets 4 à 7)

| Octet | Bits | Champ Victron | Condition évaluée | Registres TinyBMS |
| --- | --- | --- | --- | --- |
| 4 | 0-1 | General Warning | Reflète le niveau maximal (0, 1 ou 2) détecté parmi les warnings. | Basé sur les mêmes mesures que les warnings ci-dessous. |
| 4 | 2-3 | Battery High Voltage Warning | `pack_voltage_v ≥ 0.95 × overvoltage_cutoff_v`. | 0x0024, 0x013B. |
| 4 | 4-5 | Battery Low Voltage Warning | `pack_voltage_v ≤ 1.05 × undervoltage_cutoff_v`. | 0x0024, 0x013C. |
| 4 | 6-7 | Battery High Temp Warning | `max_temp ≥ 0.9 × overheat_cutoff_c`. | 0x0030, 0x0071, 0x013F. |
| 5 | 0-1 | Battery Low Temp Warning | `min_temp ≤ 0 °C`. | 0x0030, 0x0071. |
| 5 | 2-3 | Charge High Temp Warning | `aux_temp ≥ 0.9 × overheat_cutoff_c`. | 0x002B, 0x013F. |
| 5 | 4-5 | Charge Low Temp Warning | `aux_temp ≤ low_temp_charge_cutoff_c + 5 °C`. | 0x002B, 0x0140 (coupure basse charge). |
| 5 | 6-7 | Battery High Current Warning | `|-pack_current_a| ≥ 0.8 × discharge_overcurrent_limit_a`. | 0x0026, 0x013D. |
| 6 | 0-1 | Battery High Charge Current Warning | `pack_current_a ≥ 0.8 × charge_overcurrent_limit_a`. | 0x0026, 0x013E. |
| 6 | 2-7 | Reserved | Champs warning charge/contactor/short forcés à `11b`. | — |
| 7 | 0-1 | Cell Imbalance Warning | Δcellules ≥ 40 mV (niveau 1) ou ≥ 80 mV (niveau 2). | 0x0028, 0x0029. |
| 7 | 2-7 | Reserved | Bits supérieurs bloqués à `11b`, autres non utilisés. | — |

## Notes complémentaires

* Les seuils de température utilisent les valeurs TinyBMS en degrés Celsius. Les multiplications (0.9, 0.95, 1.05, etc.) sont appliquées sur les seuils TinyBMS avant comparaison.
* Lorsque plusieurs capteurs sont mentionnés (MOSFET, pack min/max), la condition utilise le maximum ou le minimum selon le contexte pour refléter la mesure la plus critique.
* Les champs réservés sont explicitement forcés à `0b11` conformément à la spécification Victron, afin de distinguer ces bits des alarmes effectivement mises en œuvre.
