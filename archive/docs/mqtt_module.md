# Module MQTT

Ce document décrit la configuration, l'utilisation et les procédures de test du module MQTT du projet TinyBMS Web Gateway.

## Configuration

Le module MQTT s'appuie sur la structure `mqtt_client_config_t` pour stocker l'URI du broker, les identifiants facultatifs, l'intervalle de keepalive et la politique de QoS/retain appliquée par défaut aux publications.【F:main/mqtt_client/mqtt_client.h†L31-L47】

Pour activer le module, vérifier que l'option de configuration `CONFIG_TINYBMS_MQTT_ENABLE` est positionnée à `y` (valeur par défaut dans `sdkconfig.defaults`).【F:sdkconfig.defaults†L1-L8】

Résumé des champs de configuration :

| Champ | Description | Remarques |
| --- | --- | --- |
| `broker_uri` | URI complet du broker MQTT (`mqtt://host:port` ou `mqtts://...`). | Longueur maximale : 128 caractères. |
| `username` / `password` | Identifiants optionnels si l'authentification est requise. | Longueur maximale : 64 caractères par champ. |
| `keepalive_seconds` | Intervalle keepalive négocié avec le broker. | Valeur recommandée : 30–60 s selon la stabilité réseau. |
| `default_qos` | QoS appliqué lorsqu'aucun paramètre spécifique n'est fourni. | Généralement `0` ou `1`. |
| `retain_enabled` | Active le flag retain pour les publications de statut. | Conserver à `true` pour exposer l'état le plus récent. |

## Arbre des topics

Les topics publiés suivent les formats décrits dans `mqtt_topics.h` :

| Usage | Format | QoS | Retain |
| --- | --- | --- | --- |
| Statut global | `bms/<device-id>/status` | 1 | Oui |
| Flux de métriques | `bms/<device-id>/metrics` | 0 | Non |
| Trame CAN brute | `bms/<device-id>/can/raw` | 0 | Non |
| Trame CAN décodée | `bms/<device-id>/can/decoded` | 0 | Non |
| Signal « CAN prêt » | `bms/<device-id>/can/ready` | 0 | Non |
| Configurations appliquées | `bms/<device-id>/config` | 1 | Non |

Ces paramètres sont consolidés dans le statut courant accessible via `mqtt_gateway_get_status`, qui expose l'URI du broker, les topics en cours d'utilisation et divers compteurs (reconnexions, erreurs, etc.).【F:main/mqtt_gateway/mqtt_gateway.h†L12-L38】

## Utilisation

1. Configurer l'URI du broker et les identifiants via l'outil de configuration ou le gestionnaire embarqué.
2. Déployer le firmware avec `CONFIG_TINYBMS_MQTT_ENABLE=y` puis démarrer l'équipement.
3. Vérifier la connexion initiale en consultant les logs pour un événement `MQTT_CLIENT_EVENT_CONNECTED` ou en interrogeant `mqtt_gateway_get_status` via l'API interne.
4. Souscrire aux topics souhaités (`status`, `metrics`, `can/#`, `config`) depuis votre consommateur MQTT.

## Tests manuels avec Mosquitto

1. Lancer un broker Mosquitto local :
   ```bash
   mosquitto -p 1883
   ```
2. Depuis un terminal, souscrire aux topics clés :
   ```bash
   mosquitto_sub -t "bms/<device-id>/#" -v
   ```
3. Démarrer le gateway TinyBMS et vérifier la réception des messages sur le terminal de souscription.
4. Publier un message de configuration de test pour valider la voie montante :
   ```bash
   mosquitto_pub -t "bms/<device-id>/config" -m '{"demo":true}' -q 1
   ```
5. Observer la réaction du module (journalisation ou effet attendu) et s'assurer que `retain_enabled` applique la politique attendue sur le topic de statut.

## Scénarios de déconnexion / reconnexion

Les compteurs `reconnect_count`, `disconnect_count`, `error_count` et le dernier événement (`last_event`) permettent de vérifier la résilience du module face aux pertes réseau.【F:main/mqtt_gateway/mqtt_gateway.h†L12-L38】 Pour valider le comportement :

1. Déconnecter le broker (arrêt du service Mosquitto ou coupure réseau) et confirmer l'incrément de `disconnect_count` dans le statut.
2. Relancer le broker et vérifier l'incrément de `reconnect_count` ainsi que la remise à jour du topic de statut.
3. Répéter avec des pertes brèves pour s'assurer que le module se reconnecte sans intervention manuelle.
4. Surveiller `last_error` dans le statut pour consigner les causes éventuelles d'échec.【F:main/mqtt_gateway/mqtt_gateway.h†L12-L38】

## Tests automatisés

Des tests unitaires couvrent désormais l'initialisation, la configuration, la publication et l'enregistrement de callbacks du client MQTT. Ils peuvent être exécutés via la suite Unity du projet (voir section Tests du dépôt) pour vérifier la robustesse des fonctions publiques avant déploiement.【F:test/test_mqtt_client.c†L1-L119】
