# TinyBMS_Web_Gateway
TinyBMS Web Gateway : Passerelle intelligente UART → Victron CAN Bus avec interface web locale, monitoring temps réel et architecture événementielle 100% ESP-IDF

Objectif principal
Développer une passerelle embarquée autonome basée sur la carte ESP32-CAN-X2 permettant :
	1	L’interfaçage complet entre un TinyBMS (via UART) et un système Victron Energy (via CAN Bus au standard VE.Can / BMS-Can).
	2	La conversion bidirectionnelle d’un ensemble défini de PGN (Parameter Group Numbers) critiques du protocole Victron BMS-Can.
	3	Une interface web locale (serveur HTTP + WebSocket) hébergée sur l’ESP32 pour :
	◦	Configuration en temps réel du système (baudrates, filtres PGN, seuils d’alerte, etc.)
	◦	Monitoring graphique et numérique des données BMS (SOC, tension, courant, température, état des cellules, alarmes)
	◦	Reporting historique (logs, courbes, export CSV)
	◦	Mise à jour firmware OTA (Over-The-Air)

Contexte technique
	•	Matériel : ESP32-CAN-X2 (dual-core Xtensa LX7, CAN natif, Wi-Fi/BLE, 4 Mo Flash)
	•	BMS : TinyBMS (communication série UART, protocole propriétaire documenté)
	•	Système cible : Écosystème Victron Energy (GX devices, Cerbo GX, MultiPlus, etc.)
	•	Protocole CAN : BMS-Can (500 kbps, format Victron PGN, basé sur NMEA 2000 / J1939)
	•	Framework : 100% ESP-IDF v5.x (pas d’Arduino) avec HAL, RTOS, non bloquante, et architecture modulaire événementielle

Architecture logicielle (100% ESP-IDF)
+--------------------------------------------------------------------+
|   Web Server                                                       | ← HTTP + WebSocket + SPIFFS (UI statique)
+---------+----------------------------------------------------------+
          ↓
+--------------------------------------------------------------------+
|                    Event Bus (Pub/Sub)                             |   ← FreeRTOS Queue + Event Groups + Callbacks 
+---------+----------------------------------------------------------+
     ↑        ↑        ↑
     |        |        |
+---v---+  +--v--+  +--v--+
| UART  |  | CAN |  | CFG |
| Module|  | Ctrl|  | Mgr |
+-------+  +-----+  +-----+
     ↑        ↑        ↑
+---v---+  +--v--+  +--v--+
| Tiny  |  | VIC |  | NVS |
| BMS   |  | PGN |  | Storage |
+-------+  +-----+  +--------+
Bus d’événements centralisé (Event Bus)
	•	Basé sur FreeRTOS queues et event groups
	•	Chaque module s’abonne aux événements qui l’intéressent
	•	Publication asynchrone avec priorité et type d’événement
	•	Événements typés : BMS_DATA_UPDATE, CAN_PGN_RX, ALARM_TRIGGER, CONFIG_CHANGED, etc.

Modules fonctionnels
Module
Responsabilité
UART TinyBMS Driver
Lecture cyclique des trames TinyBMS, parsing, validation CRC, timeout
CAN Victron Controller
Envoi/réception PGN, gestion du bus 500 kbps, filtres, heartbeat
PGN Mapper
Conversion bidirectionnelle TinyBMS ↔ Victron PGN (table de mapping configurable)
Web Server
Serveur HTTP (pages + API REST), WebSocket (push temps réel), OTA
Configuration Manager
Stockage NVS, interface web, validation, valeurs par défaut
Monitoring & Logging
Buffers circulaires, export CSV, courbes temps réel
Event Bus Core
Routage, priorisation, debug (trace événements)

PGN Victron à supporter (priorité 1)
PGN
Description
Direction
0x351
BMS State of Charge (SOC)
TX
0x352
BMS Voltage, Current
TX
0x353
BMS Alarms & Warnings
TX
0x355
BMS Cell Voltages (min/max/avg)
TX
0x356
BMS Temperature
TX
0x35A
BMS Charge/Discharge Limits
TX
0x370
BMS Name & Status
TX
0x37F
BMS Request (heartbeat)
RX/TX
Extension future : support additionnel de PGN personnalisés via JSON config

Interface web locale (sans dépendance cloud)
	•	SSID AP par défaut : TinyBMS-Gateway
	•	IP fixe en AP : 192.168.4.1
	•	Page d’accueil : Dashboard temps réel (gauges, graphiques Chart.js)
	•	Onglets :
	◦	Dashboard
	◦	Configuration (UART, CAN, PGN mapping, Wi-Fi)
	◦	Logs & Export
	◦	À propos / OTA

Contraintes techniques
Critère
Exigence
Framework
100% ESP-IDF (HAL, drivers natifs, CMake)
CAN Bus
500 kbps, terminaison 120Ω logicielle/paramétrable
UART
115200 bps, 8N1, timeout 500ms
Stockage
SPIFFS (UI) + NVS (config)
Consommation
Mode veille léger (Wi-Fi AP only si besoin)
Robustesse
Watchdog, redémarrage propre, gestion des erreurs bus
Sécurité
Mot de passe AP, validation entrées web, OTA signé (future)

Livrables attendus
	1	Code source complet (Git, branches dev, feature/*, release)
	2	Documentation :
	◦	README (setup, flash, usage)
	◦	Architecture (diagrammes)
	◦	Mapping PGN (tableau)
	◦	API Web (endpoints)
	3	Interface web responsive (mobile-friendly)
	4	Script de build automatisé (idf.py, menus config)
	5	Tests unitaires (simulables via QEMU ou mock)

Jalons proposés (Roadmap)
Phase
Durée
Objectif
P0 – Setup
1 semaine
ESP-IDF, CAN driver, UART, Event Bus basique
P1 – UART ↔ Struct
1 semaine
Parsing TinyBMS, événements BMS
P2 – CAN TX PGN
1 semaine
Envoi des 8 PGN prioritaires
P3 – Web Server
2 semaines
HTTP + WebSocket + UI basique
P4 – Config & NVS
1 semaine
Sauvegarde, interface config
P5 – Monitoring
1 semaine
Graphiques, logs, export
P6 – Robustesse
1 semaine
Watchdog, erreurs, tests terrain
P7 – OTA & Docs
1 semaine
Mise à jour firmware, doc finale

Critères de succès (DoD)
	•	Victron GX reconnaît le BMS (SOC affiché, charge contrôlée)
	•	Interface web accessible en 10s après boot
	•	Données mises à jour toutes les 1s (WebSocket)
	•	Configuration persistante après reboot
	•	Zéro crash après 24h de fonctionnement continu
	•	Code propre, modulaire, documenté

Pourquoi ce fil rouge ?
Cet énoncé est :
	•	Complet : couvre matériel, logiciel, UX, tests
	•	Modulaire : chaque module est indépendant et testable
	•	Évolutif : facile d’ajouter des PGN, capteurs, protocoles
	•	Professionnel : respecte les standards embarqués et automotive
	•	Traçable : jalons, livrables, critères clairs
Prêt à démarrer ? Clone le repo, lance idf.py menuconfig, et suis le jalon P0 !

Souhaites-tu que je te génère :
	•	Le squelette Git + CMakeLists.txt ?
	•	Le schéma Event Bus en code ?
	•	La première version du PGN Mapper ?
	•	Ou le dashboard HTML/JS de base ?
Dis-moi par où tu veux commencer !
