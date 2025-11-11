# Tâche : Corriger les appels à `uart_bms_unregister_listener`

## Résumé
Les modules `monitoring`, `pgn_mapper` et `tiny_mqtt_publisher` appellent encore l'ancienne signature de `uart_bms_unregister_listener`, ce qui bloque la compilation. Cette tâche consiste à propager la nouvelle API (callback + contexte) à tous les appelants et à supprimer l'attente d'un code retour.

## Contexte
- Nouvelle signature définie dans `main/uart_bms/uart_bms.h` :
  ```c
  void uart_bms_unregister_listener(uart_bms_listener_cb_t callback, void *ctx);
  ```
- L'inscription initiale se fait avec `uart_bms_register_listener(callback, ctx)` ; le même couple doit être fourni pour la désinscription.

## Étapes proposées
1. **Identifier le contexte d'enregistrement**
   - `monitoring_register_uart_listener()` stocke `monitoring_on_bms_update` avec `NULL` : réutiliser `NULL` lors de la désinscription dans `monitoring_deinit()`.
   - `pgn_mapper_init()` enregistre `pgn_mapper_on_bms_update` avec `NULL` : désinscrire avec `NULL` dans `pgn_mapper_deinit()`.
   - `tiny_mqtt_publisher_init()` enregistre `tiny_mqtt_publisher_on_bms_update` avec `publisher` comme contexte : passer le pointeur `publisher` à la désinscription.
2. **Mettre à jour les appels**
   - Remplacer les appels existants par `uart_bms_unregister_listener(<callback>, <ctx>)` sans capturer de valeur de retour.
   - Supprimer les blocs `if (err != ESP_OK)` ou les logs associés.
3. **Nettoyage**
   - Vérifier qu'aucun prototype local ou mock ne conserve l'ancienne signature.
   - Adapter les tests unitaires/mocks éventuels (chercher `uart_bms_unregister_listener` via `rg`).
4. **Validation**
   - Compiler le projet (`idf.py build`).
   - Lancer les tests/unités ou intégration impactés s'ils existent.

## Critères d'acceptation
- Le projet compile sans erreur liée à `uart_bms_unregister_listener`.
- Les logs ne mentionnent plus d'erreur d'appel lors de l'arrêt des modules.
- Les ressources UART sont libérées proprement à la désinitialisation des modules.

## Risques et points d'attention
- Oublier de transmettre le bon contexte pourrait laisser des callbacks résiduels actifs.
- Les mocks utilisés dans les tests (si existants) doivent refléter la nouvelle signature pour éviter des échecs à la compilation.
