# Audit automatisé TinyBMS ↔ Victron

## Synthèse de la matrice fournie

- Champs décrits : **67**
- Registres TinyBMS référencés : **19**
- CAN ID Victron distincts : **21**
- Champs nécessitant un calcul : **18**

## Vérification automatique

### Couverture des registres TinyBMS requis

| Registre | Champs concernés | Statut firmware | Commentaire |
| --- | --- | --- | --- |
| 36 | Battery High Voltage Alarm, Battery High Voltage Warning, Battery Low Voltage Alarm, Battery Low Voltage Warning, Battery Voltage | Pris en charge | Battery Pack Voltage |
| 38 | Battery Current, Battery High Charge Current Alarm, Battery High Current Alarm, Battery High Current Warning | Pris en charge | Battery Pack Current |
| 40 | Min Cell Voltage | Pris en charge | Min Cell Voltage |
| 41 | Max Cell Voltage | Pris en charge | Max Cell Voltage |
| 42 | Battery High Temp Charge Alarm, Battery High Temp Charge Warning, Battery Low Temp Charge Warning | Pris en charge | External Temperature #1 |
| 45 | State Of Health (SOH) | Pris en charge | State Of Health |
| 46 | High Resolution SOC (optionnel), State Of Charge (SOC) | Pris en charge | State Of Charge |
| 48 | Battery Temperature | Pris en charge | Internal Temperature |
| 50 | System Status (online/offline) | Pris en charge | System Status (online/offline) |
| 52 | Cell Imbalance Alarm | Pris en charge | Real Balancing Bits |
| 102 | Max Discharge Current Limit (DCL) | Manquant | Non lu par la pile UART |
| 103 | Max Charge Current Limit (CCL) | Manquant | Non lu par la pile UART |
| 113 | Battery High Temp Alarm, Battery High Temp Warning, Battery Low Temp Alarm, Battery Low Temp Warning, Highest Cell Temperature, Lowest Cell Temperature | Pris en charge | Pack Temperature Min |
| 306 | Installed Capacity, Online Capacity | Pris en charge | Battery Capacity |
| 500 | Manufacturer Name | Pris en charge | Hardware/Changes Version |
| 501 | Firmware Version | Pris en charge | Public Firmware/Flags |
| 502 | Battery Family Name | Pris en charge | Internal Firmware Version |
| 504 | Serial Number Part 1 | Interrogé (sans métadonnées) | Présent dans la liste de poll, pas de décodage dédié |
| 505 | Serial Number Part 2 | Interrogé (sans métadonnées) | Présent dans la liste de poll, pas de décodage dédié |

### Couverture des trames CAN Victron

| CAN ID (PGN) | Champs (comptés) | Statut firmware | Commentaire |
| --- | --- | --- | --- |
| 0x305 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x307 | 3 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x351 | 4 | Publié | Victron charge/discharge limits |
| 0x355 | 3 | Publié | Victron SOC/SOH |
| 0x356 | 3 | Publié | Victron voltage/current/temperature |
| 0x35A | 29 | Publié | Victron alarm summary |
| 0x35E | 1 | Publié | Victron manufacturer string |
| 0x35F | 3 | Publié | Victron battery identification |
| 0x370 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x371 | 1 | Publié | Victron battery info part 2 |
| 0x372 | 4 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x373 | 4 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x374 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x375 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x376 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x377 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x378 | 2 | Publié | Victron energy counters |
| 0x379 | 1 | Publié | Victron installed capacity |
| 0x380 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x381 | 1 | Manquant | Aucune entrée dans g_can_publisher_channels |
| 0x382 | 1 | Publié | Victron battery family |

### Registres manquants à implémenter

- Reg 102, Reg 103 : absents des métadonnées et de la lecture UART.

### CAN ID manquants

- 0x305, 0x307, 0x370, 0x372, 0x373, 0x374, 0x375, 0x376, 0x377, 0x380, 0x381 : aucune trame publiée pour ces identifiants.

_Rapport généré automatiquement par `tools/audit_mapping.py` à partir de `docs/TinyBMS_CAN_BMS_mapping.json`._
