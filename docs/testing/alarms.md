# Tests d'alarmes TinyBMS Gateway

Ce document regroupe les scénarios de test associés aux alarmes critiques décrites dans `../acceptance_criteria.md`.

## Scénarios automatisés

1. **Détection surcharge courant** : injecter trame UART `0x2002` > limite, vérifier flag MQTT `bms/alarms/current`. 
2. **Surchauffe cellule** : forcer `0x2101` au-dessus du seuil, confirmer émission CAN PGN 0x35E et coupure relais.
3. **Delta tension cellules** : rejouer capture `uart_request/delta_cells.json`, vérifier alerte équilibrage dans `bridge_cvl.cpp`.

## Journalisation

- Les tests doivent écrire les horodatages d'activation dans `logs/alarms.log` et archiver les traces UART/CAN.
- Les écarts d'horloge sont vérifiés via `tools/check_timestamps.py` (tolérance ±1 s).

## Validation

- Les résultats sont annexés au rapport de validation qualité et signés électroniquement.

