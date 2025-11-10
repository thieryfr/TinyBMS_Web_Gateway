# Cartographie des registres TinyBMS, CAN Victron et flux applicatifs

Cette page documente l'utilisation exhaustive des registres TinyBMS interrogés par la passerelle, la correspondance avec les identifiants CAN Victron ainsi que les flux montants et descendants vers le Cerbo GX et l'interface Web.

## 1. Vue d'ensemble des flux

### 1.1 TinyBMS vers Victron Cerbo GX

1. `uart_bms` interroge 59 mots Modbus et décode chaque registre dans `uart_bms_live_data_t` (tensions, courants, états, métadonnées ASCII).【F:main/uart_bms/uart_bms_protocol.c†L6-L139】【F:main/uart_bms/uart_response_parser.cpp†L73-L325】
2. `can_publisher` est inscrit comme listener UART et encode chaque mise à jour TinyBMS en trames Victron (`can_publisher_on_bms_update` + fonctions `encode_*`).【F:main/can_publisher/can_publisher.c†L188-L317】【F:main/can_publisher/conversion_table.c†L760-L940】【F:main/can_publisher/conversion_table.c†L1342-L1413】
3. Chaque trame prête publie `APP_EVENT_ID_CAN_FRAME_READY` et est remise au driver `can_victron_publish_frame()` pour émission TWAI.【F:main/can_publisher/can_publisher.c†L100-L168】【F:main/include/app_events.h†L27-L44】【F:main/can_victron/can_victron.c†L258-L357】
4. `can_victron` diffuse en parallèle les trames TX/RX (évènements `RAW` et `DECODED`) consommées par MQTT et la Web UI.【F:main/can_victron/can_victron.c†L258-L357】【F:main/mqtt_gateway/mqtt_gateway.c†L556-L609】【F:main/web_server/web_server.c†L2488-L2558】

### 1.2 TinyBMS ↔ Interface Web

*Flux montants* (télémétrie vers la Web UI)

- `uart_bms` publie `APP_EVENT_ID_BMS_LIVE_DATA` et un flux JSON de télémétrie. `web_server` retransmet ces évènements sur `/ws/telemetry`, `/ws/uart`, `/ws/can` suivant le type (`CAN_FRAME_RAW/DECODED`).【F:main/include/app_events.h†L21-L43】【F:main/web_server/web_server.c†L2285-L2558】
- L'API REST expose `/api/registers` (catalogue de registres + valeurs persistées) et `/api/can/status` pour l'état du bus CAN.【F:main/web_server/web_server.c†L1991-L2051】【F:main/web_server/web_server.c†L2711-L2723】

*Flux descendants* (actions UI → TinyBMS)

- Les mises à jour JSON envoyées sur `POST /api/registers` sont traduites par `config_manager_apply_register_update_json()` qui écrit sur le registre TinyBMS via `uart_bms_write_register()` avant de persister la valeur et d'émettre `register_update`.【F:main/web_server/web_server.c†L2007-L2051】【F:main/config_manager/config_manager.c†L2113-L2191】
- Les commandes système (redémarrage, OTA, etc.) suivent le même bus d'évènements mais ne modifient pas les registres TinyBMS.

## 2. Registres TinyBMS interrogés (lecture)

Le tableau ci-dessous regroupe les 59 mots interrogés périodiquement. Les numéros de registre sont indiqués **comme dans la documentation _TinyBMS Communication Protocols_ (décimal)** avec leur équivalent hexadécimal pour correspondre au code.

| Registre(s) TinyBMS (dec / hex) | Type | Champ principal | Usage / consommateurs |
| --- | --- | --- | --- |
| 0–15 / 0x0000–0x000F | `uint16`, 0.1 mV | Tensions cellules individuelles | Stockées dans `cell_voltage_mv[]` et utilisées pour les extrêmes, identifiants min/max et équilibreur CAN (PGN 0x373–0x377).【F:main/uart_bms/uart_bms_protocol.c†L6-L101】【F:main/uart_bms/uart_response_parser.cpp†L258-L316】【F:main/can_publisher/conversion_table.c†L940-L1178】
| 32–33 / 0x0020–0x0021 | `uint32` | Compteur vie (uptime) | Mis à jour dans `uptime_seconds`, visible en télémétrie et export MQTT/REST.【F:main/uart_bms/uart_bms_protocol.c†L200-L213】【F:main/uart_bms/uart_response_parser.cpp†L360-L403】
| 34–35 / 0x0022–0x0023 | `uint32` | Temps estimé restant | Exposé via télémétrie et Web UI (diagnostic autonomie).【F:main/uart_bms/uart_bms_protocol.c†L213-L226】【F:main/uart_bms/uart_response_parser.cpp†L360-L403】
| 36–37 / 0x0024–0x0025 | `float32` | Tension pack | Source des PGN 0x356 (voltage/current) et affichage UI.【F:main/uart_bms/uart_bms_protocol.c†L226-L239】【F:main/uart_bms/uart_response_parser.cpp†L404-L434】【F:main/can_publisher/conversion_table.c†L893-L940】
| 38–39 / 0x0026–0x0027 | `float32` | Courant pack | Contribue à PGN 0x356, limites CVL/DCL et énergie cumulée.【F:main/uart_bms/uart_bms_protocol.c†L239-L252】【F:main/uart_bms/uart_response_parser.cpp†L404-L434】【F:main/can_publisher/conversion_table.c†L760-L848】【F:main/can_publisher/conversion_table.c†L1006-L1106】
| 40 / 0x0028 | `uint16` | Min cellule | Sert aux PGN extrêmes 0x373/0x374 et aux alertes UI.【F:main/uart_bms/uart_bms_protocol.c†L252-L264】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L940-L1037】
| 41 / 0x0029 | `uint16` | Max cellule | Utilisé dans PGN 0x373/0x375.【F:main/uart_bms/uart_bms_protocol.c†L264-L276】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L940-L1037】
| 42 / 0x002A | `int16`, 0.1 °C | Température externe 1 | Affichage UI, PGN 0x373 via moyenne pack.【F:main/uart_bms/uart_bms_protocol.c†L276-L289】【F:main/uart_bms/uart_response_parser.cpp†L316-L359】【F:main/can_publisher/conversion_table.c†L940-L1037】
| 43 / 0x002B | `int16`, 0.1 °C | Température externe 2 | Télémétrie secondaire (dashboard UI).【F:main/uart_bms/uart_bms_protocol.c†L289-L302】【F:main/uart_bms/uart_response_parser.cpp†L316-L359】
| 45 / 0x002D | `uint16`, 0.002 % | SOH | PGN 0x355 et UI.【F:main/uart_bms/uart_bms_protocol.c†L302-L314】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L848-L893】
| 46 / 0x002E–0x002F | `uint32`, 1e-6 % | SOC haute résolution | PGN 0x355 (SOC + résolution) et jauge UI.【F:main/uart_bms/uart_bms_protocol.c†L314-L327】【F:main/uart_bms/uart_response_parser.cpp†L360-L403】【F:main/can_publisher/conversion_table.c†L848-L893】
| 48 / 0x0030 | `int16`, 0.1 °C | Température interne MOSFET | PGN 0x356 (octets 4-5) et alarme thermique UI.【F:main/uart_bms/uart_bms_protocol.c†L327-L339】【F:main/uart_bms/uart_response_parser.cpp†L316-L359】【F:main/can_publisher/conversion_table.c†L893-L940】
| 50 / 0x0032 | `uint16` | Statut système / alarmes | Transformé en bits d'alarmes pour PGN 0x35A et notifications.【F:main/uart_bms/uart_bms_protocol.c†L339-L351】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L914-L1006】
| 51 / 0x0033 | `uint16` | Besoin équilibrage | Indique warnings UI et PGN 0x35A.【F:main/uart_bms/uart_bms_protocol.c†L351-L363】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L914-L1006】
| 52 / 0x0034 | `uint16` | Bits équilibrage effectif | Répliqués sur `cell_balancing[]` et PGN 0x35A.【F:main/uart_bms/uart_bms_protocol.c†L363-L375】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L914-L1006】
| 102 / 0x0066 | `uint16`, 0.1 A | Limite décharge | Utilisé pour PGN 0x351 (DCL) et UI limites.【F:main/uart_bms/uart_bms_protocol.c†L375-L387】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】
| 103 / 0x0067 | `uint16`, 0.1 A | Limite charge | Utilisé pour PGN 0x351 (CCL) et UI.【F:main/uart_bms/uart_bms_protocol.c†L387-L399】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】
| 113 / 0x0071 | `int8` paire | Températures pack min/max | PGN 0x373 et UI thermiques.【F:main/uart_bms/uart_bms_protocol.c†L399-L415】【F:main/uart_bms/uart_response_parser.cpp†L435-L481】【F:main/can_publisher/conversion_table.c†L940-L1037】
| 305 / 0x0131 | `uint16`, 1 A | Pic coupure décharge | Fallback limites CAN si non fourni par config.【F:main/uart_bms/uart_bms_protocol.c†L415-L427】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】
| 306 / 0x0132 | `uint16`, 0.01 Ah | Capacité batterie | PGN 0x35F (octets 4-5) et UI capacité.【F:main/uart_bms/uart_bms_protocol.c†L427-L439】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L806-L848】
| 307 / 0x0133 | `uint16` | Nombre cellules série | Export télémétrie (diagnostic).【F:main/uart_bms/uart_bms_protocol.c†L439-L451】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】
| 315 / 0x013B | `uint16` | Seuil surtension | Sert au calcul CVL si CVL absent.【F:main/uart_bms/uart_bms_protocol.c†L451-L463】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】
| 316 / 0x013C | `uint16` | Seuil sous-tension | UI & PGN limites.【F:main/uart_bms/uart_bms_protocol.c†L463-L475】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】
| 317 / 0x013D | `uint16` | Coupure sur-courant décharge | Fallback DCL.【F:main/uart_bms/uart_bms_protocol.c†L475-L487】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】
| 318 / 0x013E | `uint16` | Coupure sur-courant charge | Fallback CCL.【F:main/uart_bms/uart_bms_protocol.c†L487-L499】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】
| 319 / 0x013F | `int16` | Coupure surchauffe | UI + PGN 0x35A bits température.【F:main/uart_bms/uart_bms_protocol.c†L499-L511】【F:main/uart_bms/uart_response_parser.cpp†L316-L359】
| 320 / 0x0140 | `int16` | Coupure basse température charge | UI + log évènements.【F:main/uart_bms/uart_bms_protocol.c†L511-L523】【F:main/uart_bms/uart_response_parser.cpp†L316-L359】
| 500 / 0x01F4 | `uint16` | Version HW & changements | PGN 0x35F/handshake ASCII.【F:main/uart_bms/uart_bms_protocol.c†L523-L536】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】【F:main/can_publisher/conversion_table.c†L640-L738】
| 501 / 0x01F5 | `uint16` | Firmware public + flags | PGN 0x35F.【F:main/uart_bms/uart_bms_protocol.c†L536-L548】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L760-L848】
| 502 / 0x01F6 | `uint16` | Firmware interne | PGN 0x35F et nom batterie ASCII.【F:main/uart_bms/uart_bms_protocol.c†L548-L560】【F:main/uart_bms/uart_response_parser.cpp†L268-L316】【F:main/can_publisher/conversion_table.c†L640-L725】
| 503–505 / 0x01F7–0x01F9 | Fenêtre ASCII | Nom batterie / famille | Décodé via `decode_ascii_field` pour PGN 0x370/0x371/0x382.【F:main/uart_bms/uart_bms_protocol.c†L556-L566】【F:main/uart_bms/uart_response_parser.cpp†L482-L548】【F:main/can_publisher/conversion_table.c†L640-L738】
| 506–511 / 0x01FA–0x01FF | Fenêtre ASCII | Numéro de série | Concaténée pour PGN 0x380/0x381 et UI.【F:main/uart_bms/uart_bms_protocol.c†L556-L566】【F:main/uart_bms/uart_response_parser.cpp†L482-L548】【F:main/can_publisher/conversion_table.c†L738-L768】

## 3. Registres TinyBMS configurables (écriture)

Les registres exposés en écriture via la Web UI proviennent du catalogue `config_manager`. Chaque entrée impose bornes, pas et unités avant de déclencher `uart_bms_write_register()`.

| Registre (dec / hex) | Clé API | Description | Contraintes |
| --- | --- | --- | --- |
| 300 / 0x012C | `fully_charged_voltage_mv` | Tension cellule pleine charge | 1200 ≤ valeur ≤ 4500 mV (pas 10 mV).【F:main/config_manager/generated_tiny_rw_registers.inc†L134-L166】
| 301 / 0x012D | `fully_discharged_voltage_mv` | Tension cellule déchargée | 1000–3500 mV (pas 10 mV).【F:main/config_manager/generated_tiny_rw_registers.inc†L167-L199】
| 303 / 0x012F | `early_balancing_threshold_mv` | Seuil d'équilibrage précoce | 1000–4500 mV (pas 10 mV).【F:main/config_manager/generated_tiny_rw_registers.inc†L200-L232】
| 304 / 0x0130 | `charge_finished_current_ma` | Courant de fin de charge | 100–5000 mA (pas 10 mA).【F:main/config_manager/generated_tiny_rw_registers.inc†L233-L265】
| 305 / 0x0131 | `peak_discharge_current_cutoff_a` | Coupure pic décharge | 1–600 A (pas 1 A).【F:main/config_manager/generated_tiny_rw_registers.inc†L266-L298】
| 306 / 0x0132 | `battery_capacity_ah` | Capacité nominale | 0.0–655.35 Ah (pas 0.01 Ah).【F:main/config_manager/generated_tiny_rw_registers.inc†L299-L331】
| 307 / 0x0133 | `series_cell_count` | Nombre cellules série | 1–32 cellules (entier).【F:main/config_manager/generated_tiny_rw_registers.inc†L332-L364】
| 308 / 0x0134 | `shunt_resistance_mohm` | Résistance shunt | Contraintes spécifiques (mΩ).【F:main/config_manager/generated_tiny_rw_registers.inc†L365-L397】
| … | … | … | … |

Le catalogue complet (plus de 40 registres configurables) reste disponible dans `main/config_manager/generated_tiny_rw_registers.inc` pour référence détaillée (bornes supplémentaires, valeurs énumérées, flags).【F:main/config_manager/generated_tiny_rw_registers.inc†L134-L851】

> **Remarque :** Le fichier généré liste l'intégralité des registres RW TinyBMS pris en charge (voir `generated_tiny_rw_registers.inc`). L'API `POST /api/registers` vérifie la clé, convertit la valeur en brut, écrit le registre via `uart_bms_write_register()` et publie un évènement de mise à jour pour la Web UI.【F:main/config_manager/config_manager.c†L2113-L2191】

## 4. Correspondance CAN Victron

### 4.1 Paramètres bus et format de trame

- Le driver `can_victron` initialise l'interface TWAI ESP-IDF avec la configuration `TWAI_TIMING_CONFIG_250KBITS()`, soit un débit de **250 kbit/s**. Ce choix est également reflété dans la constante `CAN_VICTRON_BITRATE_BPS` à 250 000.【F:main/can_victron/can_victron.c†L29-L42】【F:main/can_victron/can_victron.c†L445-L455】
- Les trames dont l'identifiant dépasse 0x7FF sont marquées en mode **29 bits (extended)** avant émission (`TWAI_MSG_FLAG_EXTD`). Seules les trames de keepalive Victron (ID 0x305) restent en 11 bits standard.【F:main/can_victron/can_victron.c†L525-L536】【F:main/can_victron/can_victron.c†L836-L848】
- Les PGN Victron sont convertis en identifiants 29 bits via `VICTRON_EXTENDED_ID`, en combinant priorité, PGN et adresse source conformément au format Victron CAN-bus.【F:main/can_publisher/conversion_table.c†L60-L75】【F:main/can_publisher/conversion_table.c†L1342-L1499】

### 4.2 Trames émises

Le tableau suivant récapitule les PGN/ID transmis, leur période et les données TinyBMS utilisées.

| PGN / ID CAN | DLC | Période | Encodeur | Registres / champs TinyBMS |
| --- | --- | --- | --- | --- |
| 0x307 (standard) | 8 | 1 s | `encode_inverter_identifier` | Version HW (0x01F4), firmware public (0x01F5), ASCII handshake config.【F:main/can_publisher/conversion_table.c†L760-L821】【F:main/can_publisher/conversion_table.c†L1342-L1353】
| 0x351 (0x18FF51E5) | 8 | 1 s | `encode_charge_limits` | Tension pack / seuils (0x0024, 0x013B), limites courant (0x0066–0x0131), calcul CVL/CCL/DCL.【F:main/can_publisher/conversion_table.c†L792-L848】【F:main/can_publisher/conversion_table.c†L1353-L1362】
| 0x355 (0x18FF55E5) | 8 | 1 s | `encode_soc_soh` | SOC 0x002E, SOH 0x002D + version haute résolution.【F:main/can_publisher/conversion_table.c†L848-L893】【F:main/can_publisher/conversion_table.c†L1362-L1371】
| 0x356 (0x18FF56E5) | 8 | 1 s | `encode_voltage_current_temperature` | Tension/courant/température MOSFET (0x0024–0x0030).【F:main/can_publisher/conversion_table.c†L893-L940】【F:main/can_publisher/conversion_table.c†L1371-L1380】
| 0x35A (0x18FF5AE5) | 8 | 1 s | `encode_alarm_status` | Bits statut 0x0032–0x0034 + limites thermiques (0x013F–0x0140).【F:main/can_publisher/conversion_table.c†L914-L1006】【F:main/can_publisher/conversion_table.c†L1380-L1389】
| 0x35E (0x18FF5EE5) | 8 | 2 s | `encode_manufacturer_string` | Fenêtre ASCII 0x01F4/0x01F5 ou config.【F:main/can_publisher/conversion_table.c†L1006-L1044】【F:main/can_publisher/conversion_table.c†L1389-L1398】
| 0x35F (0x18FF5FE5) | 8 | 2 s | `encode_battery_identification` | HW/FW (0x01F4–0x01F6), capacité 0x0132, numéro série fallback.【F:main/can_publisher/conversion_table.c†L1044-L1110】【F:main/can_publisher/conversion_table.c†L1398-L1407】
| 0x370 / 0x371 (0x18FF70/71E5) | 8 | 2 s | `encode_battery_name_part1/2` | Fenêtre ASCII 0x01F6–0x01F9 ou config.【F:main/can_publisher/conversion_table.c†L640-L725】【F:main/can_publisher/conversion_table.c†L1407-L1424】
| 0x372 (0x18FF72E5) | 8 | 1 s | `encode_module_status_counts` | Alarmes/warnings, uptime, cycles (0x0020, 0x0032-0x0034).【F:main/can_publisher/conversion_table.c†L1110-L1158】【F:main/can_publisher/conversion_table.c†L1424-L1433】
| 0x373–0x377 | 8 | 1 s | `encode_cell_voltage_temperature_extremes`, `encode_min/max_cell_identifier`, `encode_min/max_temp_identifier` | Cellules min/max (0x0000–0x000F), températures pack (0x0071).【F:main/can_publisher/conversion_table.c†L940-L1006】【F:main/can_publisher/conversion_table.c†L1158-L1326】
| 0x378 (0x18FF78E5) | 8 | 1 s | `encode_energy_counters` | Intégration courant/puissance (dérivée des champs pack).【F:main/can_publisher/conversion_table.c†L300-L738】【F:main/can_publisher/conversion_table.c†L1326-L1342】
| 0x379 (0x18FF79E5) | 8 | 5 s | `encode_installed_capacity` | Capacité installée (0x0132) et heuristiques énergie.【F:main/can_publisher/conversion_table.c†L806-L848】【F:main/can_publisher/conversion_table.c†L1342-L1353】
| 0x380 / 0x381 (0x18FF80/81E5) | 8 | 5 s | `encode_serial_number_part1/2` | Fenêtre ASCII 0x01FA–0x01FF.【F:main/can_publisher/conversion_table.c†L738-L768】【F:main/can_publisher/conversion_table.c†L1353-L1371】
| 0x382 (0x18FF82E5) | 8 | 5 s | `encode_battery_family` | Fenêtre ASCII 0x01F8–0x01FF ou config.【F:main/can_publisher/conversion_table.c†L726-L738】【F:main/can_publisher/conversion_table.c†L1371-L1389】

## 5. Distribution vers MQTT et Web UI

- `mqtt_gateway` publie les flux `can/raw`, `can/decoded`, `can/ready` en fonction des évènements 0x1200–0x1202 et expose les topics configurables dans `GET /api/mqtt/config`.【F:main/mqtt_gateway/mqtt_gateway.c†L556-L609】
- `web_server` souscrit à tous les évènements et diffuse selon le type : télémétrie (`APP_EVENT_ID_TELEMETRY_SAMPLE`) vers `/ws/telemetry`, trames UART `/ws/uart`, trames CAN `/ws/can`. Les messages de configuration et diagnostics sont poussés sur `/ws/events`.【F:main/web_server/web_server.c†L2285-L2558】
- Les clients Web peuvent consulter ou modifier la configuration via `/api/config` et `/api/registers`, l'état du bus CAN via `/api/can/status`, et lancer les actions OTA/restart. Toutes les routes sont enregistrées lors de `web_server_init()`.【F:main/web_server/web_server.c†L2615-L2868】

Cette cartographie permet de suivre précisément le parcours de chaque registre TinyBMS, depuis la lecture UART jusqu'à sa diffusion sur le bus CAN Victron, MQTT et l'interface Web, ainsi que les chemins d'écriture depuis l'UI vers les registres configurables.
