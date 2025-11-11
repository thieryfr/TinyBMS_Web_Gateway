# Revue finale TinyBMS-GW (hors sécurité)

## Problèmes Critiques

### 1. API `uart_bms_unregister_listener` non mise à jour dans plusieurs modules
- **Description** : L'API `uart_bms_unregister_listener` a été modifiée pour prendre un contexte en second paramètre et ne plus renvoyer de valeur. Plusieurs modules continuent pourtant d'appeler l'ancienne signature (sans contexte et en supposant un retour `esp_err_t`). La compilation échoue immédiatement avec "too few arguments" / "void value not ignored" dès que `monitoring.c`, `pgn_mapper.c` ou `tiny_mqtt_publisher.c` sont construits.
- **Localisation** : `monitoring_deinit` (`main/monitoring/monitoring.c`, lignes 725-733) ; `pgn_mapper_deinit` (`main/pgn_mapper/pgn_mapper.c`, lignes 47-54) ; `tiny_mqtt_publisher_deinit` (`main/mqtt/tiny_mqtt_publisher.c`, lignes 300-306). Signature attendue dans `main/uart_bms/uart_bms.h`, lignes 82-92.【F:main/monitoring/monitoring.c†L725-L733】【F:main/pgn_mapper/pgn_mapper.c†L33-L54】【F:main/mqtt/tiny_mqtt_publisher.c†L300-L314】【F:main/uart_bms/uart_bms.h†L82-L113】
- **Impact** : Build bloqué → aucun firmware générable tant que les appels ne sont pas corrigés.
- **Solution proposée** : Mettre à jour chaque appel pour fournir le contexte initial enregistré (ou `NULL` si non utilisé) et supprimer toute assignation de retour. Exemple :
  ```c
  uart_bms_unregister_listener(monitoring_on_bms_update, NULL);
  ```
  Supprimer également les blocs `if (err != ESP_OK)` devenus inutiles.

## Problèmes Élevés

*(Aucun nouveau problème élevé identifié en dehors de la catégorie sécurité explicitement exclue pour cette revue finale.)*

## Problèmes Moyens

### 2. Chargement d'archives d'historique coûteux en mémoire/CPU
- **Description** : `history_logger_load_archive()` lit l'intégralité d'un fichier JSONL et parse chaque ligne via `cJSON_Parse` avant de remplir un tampon circulaire alloué par `calloc`. Sur un ESP32, parser des centaines de lignes JSON à chaque requête web peut rapidement consommer plusieurs centaines de millisecondes et ~64 kio de RAM (1 024 échantillons × ~64 octets). Il n'y a pas de limite stricte sur la taille du fichier : un log volumineux force quand même le parse complet.
- **Localisation** : `main/monitoring/history_logger.c`, lignes 719-782.【F:main/monitoring/history_logger.c†L719-L782】
- **Impact** : Pics CPU/heap lors de l'export d'historique ; risque de timeout HTTP ou de reset watchdog si le fichier grossit.
- **Solution proposée** :
  - Limiter explicitement la taille de fichier lue (p. ex. via `ftell`/`fseek` ou en tronquant après N lignes).
  - Remplacer l'analyse JSON ligne à ligne par un format binaire plus léger ou un parser streaming évitant des allocations multiples (`cJSON_ParseWithOpts` + réutilisation d'arbre, ou un parser maison sur les champs nécessaires).
  - Option : stocker des échantillons déjà sérialisés en binaire et ne générer le JSON qu'à la volée pour la fenêtre demandée.

## Problèmes Faibles

### 3. Génération JSON manuelle fragile dans le monitoring
- **Description** : `monitoring_build_snapshot_json()` assemble un objet JSON complexe via plus de 60 appels `snprintf`. Chaque ajout repose sur la taille fixe du buffer (`MONITORING_SNAPSHOT_MAX_SIZE`) et retourne `ESP_ERR_INVALID_SIZE` en cas de dépassement. La lisibilité est faible et les évolutions (ajout de champs) sont propices aux erreurs de format/troncature.
- **Localisation** : `main/monitoring/monitoring.c`, lignes 206-342.【F:main/monitoring/monitoring.c†L206-L342】
- **Impact** : Dette technique ; forte probabilité de régressions lors de l'ajout de métriques ; difficile à tester.
- **Solution proposée** : Migrer vers `cJSON` (déjà dépendance du projet) ou un builder structuré pour construire l'objet. On gagne en lisibilité, en gestion automatique de la taille et en extensibilité.

## Résumé exécutif
- **Qualité globale** : Structure toujours solide, mais une régression critique côté API UART bloque la compilation. Quelques points de performance et de maintenabilité restent à adresser.
- **Priorités** :
  1. Corriger immédiatement les appels `uart_bms_unregister_listener` (bloquant build).
  2. Optimiser le chargement d'historiques avant que les fichiers ne grossissent en production.
  3. Préparer la refonte du builder JSON dans le monitoring pour faciliter les ajouts futurs.

**Note globale de qualité (hors sécurité)** : 6/10 — architecture mature, mais une incompatibilité d'API et certains points lourds à corriger avant livrable.
