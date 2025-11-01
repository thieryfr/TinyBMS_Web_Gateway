# Critères d'acceptation TinyBMS Gateway

## Registres critiques et vérification

| Registre TinyBMS | Description | Méthode de vérification | Responsable | Étape du plan |
| ---------------- | ----------- | ----------------------- | ----------- | ------------- |
| 0x2001 – Tension pack | Lecture de la tension totale pack pour la diffusion CAN et MQTT | Capture UART via banc de test + comparaison table CAN `tiny_rw_mapping.h` | Équipe ingénierie embarquée | Lecture UART |
| 0x2002 – Courant pack | Vérifier la cohérence courant entrant/sortant avant publication | Trace UART + scénario de charge/décharge contrôlé | Équipe d'essais système | Tests |
| 0x2004 – SoC calculé | Garantit l'exactitude des alarmes de seuil SOC | Lecture UART, injection valeurs simulées via `tools/uart_request` | Ingénierie embarquée | Lecture UART |
| 0x2101 – Température cellule max | Conditionne alarmes de surchauffe CAN et journalisation MQTT | Banc thermique + lecture CAN `victron_can.cpp` | Ingénierie hardware | Encodeurs CAN |
| 0x2105 – Delta tension cellules | Utilisé pour les alarmes d'équilibrage | Replay trames UART enregistrées + vérification décodeur `bridge_cvl.cpp` | Ingénierie embarquée | Encodeurs CAN |
| 0x3001 – Flags d'alarme BMS | Propagation correcte vers MQTT/REST | Tests d'intégration automatisés `docs/testing/alarms.md` | Qualité | Tests |
| 0x3002 – Etat relais charge | Commande relais charge/décharge côté passerelle | Simulation banc relais + monitoring `shared_data.h` | Ingénierie système | Tests |
| 0x4001 – Version firmware | Validation compatibilité mapping | Lecture UART + comparaison `mapping_normalized.csv` | Ingénierie embarquée | Documentation |

## Conditions de succès pour les alarmes

1. **Détection** : chaque condition critique (tension, courant, température, delta cellules, flag système) doit être détectée en moins d'un cycle de rafraîchissement UART (100 ms) et confirmée par la conversion CAN correspondante.
2. **Journalisation** : l'activation d'une alarme entraîne l'émission d'un événement MQTT et la persistance dans les journaux système (fichier `docs/testing/alarms.md`). Les horodatages doivent être synchronisés avec l'horloge passerelle (drift < 1 s).
3. **Réactions** : pour chaque alarme, la passerelle applique la stratégie définie (coupure relais, réduction courant, notification utilisateur) dans `bridge_cvl.cpp` et renvoie un accusé par MQTT dans la seconde.

## Validation des parties prenantes

- **Ingénierie** : revue croisée du mapping UART↔CAN et résultats de tests automatisés avant validation finale. Validation consignée dans le procès-verbal de revue d'ingénierie (Sprint 12).
- **Qualité** : vérification des preuves de test, contrôle des journaux d'alarme et signature du rapport `docs/testing/alarms.md`. La validation qualité est requise avant déploiement.

