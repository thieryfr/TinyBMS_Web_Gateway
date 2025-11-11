# Checklist de validation TinyBMS-GW

Cette checklist doit être exécutée avant toute mise en production ou livraison terrain. Les étapes peuvent être automatisées dans la CI ou utilisées lors de campagnes de tests manuelles.

## 1. Prérequis généraux
- [ ] Firmware compilé avec la configuration cible (`sdkconfig` approuvé).
- [ ] Version du matériel confirmée (révision carte, modules RF) et consignée.
- [ ] Accès aux identifiants réseau et certificats nécessaires.

## 2. Tests Wi-Fi
- [ ] Vérifier la force du signal et la stabilité sur le SSID de production (> -65 dBm moyen sur 5 min).
- [ ] Valider l'authentification WPA2/WPA3 avec les nouvelles politiques de mot de passe.
- [ ] Confirmer la reconnection automatique après une coupure d'alimentation ou perte de signal.
- [ ] Mesurer le temps de récupération après roaming (< 10 s).

## 3. Tests API
- [ ] Exécuter la suite de tests automatisés sur l'API REST (`tools/api_tests/`).
- [ ] Vérifier les endpoints critiques : `/system/info`, `/battery/status`, `/ota/status`.
- [ ] Valider les codes d'erreur et la gestion des réponses inattendues (timeouts, 4xx, 5xx).
- [ ] Confirmer la journalisation des requêtes sensibles dans le SIEM.

## 4. Tests OTA
- [ ] Réaliser une mise à jour OTA sur banc de test avec la version candidate.
- [ ] Vérifier la signature et l'intégrité du paquet via checksum SHA-256.
- [ ] Confirmer le retour en version précédente en cas d'échec simulé.
- [ ] Assurer que les journaux OTA sont transmis au serveur central.

## 5. Validation de performance
- [ ] Mesurer l'utilisation CPU et mémoire après 24 h de fonctionnement (< 80 % sur les pics).
- [ ] Vérifier l'absence de fuite mémoire via l'outil de profiling (`idf.py monitor --leaks`).
- [ ] Tester la charge maximale de messages CAN et MQTT sur 30 min.

## 6. Documentation et conformité
- [ ] Mettre à jour le registre de configuration avec la version firmware et la date.
- [ ] Archiver les journaux de test et les rapports OTA.
- [ ] Soumettre le rapport de validation pour approbation sécurité.

## 7. Go/No-Go
- [ ] Toutes les étapes ci-dessus cochées et signées par l'ingénieur validation.
- [ ] Accord final du responsable produit avant déploiement.
