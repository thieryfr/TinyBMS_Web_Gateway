# Banc de test UART ↔️ CAN

Ce document décrit le banc logiciel pour rejouer les trames TinyBMS sur l’UART, inspecter
la conversion CAN Victron et automatiser la validation dans TinyBMS Web Gateway.

## 1. Banc de test et scripts

| Élément | Description | Commandes principales |
| --- | --- | --- |
| Simulateur UART | `test/uart_sim.py` publie des trames 0xAA/0x09 à partir des vecteurs JSON. Il peut écrire en binaire, sur un port série USB-TTL ou simplement afficher l’hexdump. | ```sh
# Lancement en mode hexdump
python test/uart_sim.py --scenario nominal_pack

# Rejeu continu vers /dev/ttyUSB0
python test/uart_sim.py --scenario nominal_pack --repeat 0 \
    --sleep 0.5 --port /dev/ttyUSB0 --baud 115200
``` |
| Analyseur CAN | `tools/can_inspector.py` compare un log `candump -L` aux trames attendues. | ```sh
python tools/can_inspector.py capture.log --scenario nominal_pack
``` |
| Capture CAN | `candump` (via `tools/can_capture.sh`) ou tout enregistreur ISO-TP compatible. Le format attendu par l’inspecteur est `candump -L`. | ```sh
sudo ./tools/can_capture.sh can0 capture.log
``` |

### Organisation

- Les frames de référence sont stockées dans `test/reference/uart_frames.json`. Chaque
  entrée inclut les valeurs par registre TinyBMS, une description et, le cas échéant,
  des mutations (CRC inversé, troncature) pour tester les erreurs.【F:test/reference/uart_frames.json†L1-L144】
- Les attentes CAN (PGN, périodicité, payload) sont normalisées dans
  `test/reference/can_expected_snapshot.csv` pour faciliter l’analyse automatique.【F:test/reference/can_expected_snapshot.csv†L1-L8】

## 2. Cas de test UART

| Scénario | Registres clés | Objectif |
| --- | --- | --- |
| `nominal_pack` | 51,2 V, courant -12,4 A, SOC 75 %, pas d’alarme. | Vérifier la voie nominale et le recalcul des grandeurs (PGN 0x351/0x355). |
| `undervoltage_alarm` | Min cell 2,70 V, bit d’alarme undervoltage actif, courant -180 A. | Déclencher les chemins d’alarme, vérifier la propagation CAN et MQTT. |
| `temperature_limit` | Température min -15 °C, MOSFET 62 °C, statut « charge disabled ». | Tester la logique CVL et les limites thermiques extrêmes. |
| `crc_error` | Mutation `crc_flip` (dernier octet XOR 0xFF). | Valider la détection CRC et la comptabilisation des erreurs UART. |
| `truncated_payload` | Troncature après le registre 0x0030 (température MOS). | Vérifier la gestion des longueurs invalides / timeout parsing. |

Les registres sont fournis sous forme little-endian (mots 16 bits) selon l’ordre de
polling `g_uart_bms_poll_addresses`. Le simulateur refuse toute définition incomplète
(`frame ... is missing registers`).【F:test/uart_sim.py†L50-L76】【F:test/uart_sim.py†L108-L122】

## 3. Inspection des trames CAN

1. **Capture** : lancer `candump -L` ou `tools/can_capture.sh` pendant la lecture UART.
2. **Analyse** : exécuter l’inspecteur avec le(s) scénario(s) voulu(s) :
   ```sh
   python tools/can_inspector.py capture.log --scenario nominal_pack --scenario temperature_limit
   ```
3. **Résultats** : le script affiche `[OK]` pour chaque PGN attendu, et `[FAIL]` en cas
   de payload erroné, d’intervalle hors tolérance (`--tolerance-ms`, 100 ms par défaut)
   ou d’absence de trame.【F:tools/can_inspector.py†L41-L118】

Format de log : chaque ligne doit suivre `candump -L`, ex.
`(1683123456.123456) can0 18FF50E5#320C0034FCFFFFFF`. Les champs supplémentaires de
`candump` sont ignorés par l’analyseur.【F:tools/can_inspector.py†L120-L148】

## 4. Valeurs attendues et fichiers de référence

- **UART** : `uart_frames.json` couvre les registres, les attentes physiques
  (voltage, courant, SOC) et les mutations d’erreur. Ces valeurs alimentent aussi les
  tests unitaires via `test/uart_sim.py` (construction + CRC).【F:test/reference/uart_frames.json†L1-L144】【F:test/uart_sim.py†L78-L106】
- **CAN** : `can_expected_snapshot.csv` encode PGN, périodicité (1 s), payload hex et
  annotation fonctionnelle. Le parseur vérifie la périodicité par PGN et compare les
  payloads hexa exacts.【F:test/reference/can_expected_snapshot.csv†L1-L8】【F:tools/can_inspector.py†L63-L118】

Pour un enregistrement, les fichiers de référence servent aussi de base à la création
de nouveaux scénarios (ajouter une ligne CSV et une entrée JSON, puis rejouer la
capture pour compléter le coverage).

## 5. Planification et automatisation

| Fréquence | Action | Détails |
| --- | --- | --- |
| CI (à chaque MR) | `idf.py test` + `python -m compileall test/uart_sim.py tools/can_inspector.py` | Confirme que le simulateur et l’analyseur sont valides sur toutes les PR. |
| Hebdomadaire | Rejeu `test/uart_sim.py --scenario nominal_pack --output nominal.bin` + inspection CAN sur capture terrain. | Permet de détecter les dérives matérielles / firmwares TinyBMS. |
| Campagne terrain | Démarrer le simulateur sur port série, capturer CAN, archiver logs + rapport `tools/can_inspector.py`. | Assure la traçabilité des essais et alimente les rapports qualité. |

### Intégration CI recommandée

Ajouter au workflow GitHub Actions :
```yaml
- name: Validate UART/CAN tooling
  run: |
    python -m compileall test/uart_sim.py tools/can_inspector.py
    python test/uart_sim.py --scenario nominal_pack --repeat 1 --sleep 0
```
(Le dernier appel échoue si la définition JSON manque un registre ou si la génération
CRC ne fonctionne pas.)

### Campagne manuelle

1. Préparer le banc avec l’ESP32 en mode pont et un adaptateur CAN/USB.
2. Lancer `test/uart_sim.py --repeat 0 --sleep 0.5 --port /dev/ttyUSB0`.
3. Capturer `candump -L can0 > capture.log` pendant 2 à 3 minutes.
4. Vérifier `python tools/can_inspector.py capture.log --scenario nominal_pack`.
5. Archiver `capture.log`, le rapport de l’inspecteur et la version du firmware TinyBMS.
