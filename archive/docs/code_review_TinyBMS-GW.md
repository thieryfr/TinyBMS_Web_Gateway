# Revue de code TinyBMS-GW

## Problèmes critiques

### 1. Exposition des secrets Wi-Fi/MQTT via l'API de configuration
- **Description** : L'endpoint `GET /api/config` renvoie la configuration complète produite par `config_manager_get_config_json()`. Le snapshot JSON inclut en clair les mots de passe Wi-Fi station/AP ainsi que les identifiants MQTT. Côté web, la réponse est transmise telle quelle au client sans masquage ni contrôle d'accès. Une requête réseau non authentifiée peut donc récupérer les identifiants d'infrastructure et ouvrir la voie à une compromission réseau ou au détournement du broker MQTT.
- **Localisation** : Construction du snapshot JSON (`main/config_manager/config_manager.c`, lignes 1290-1340) et handler HTTP (`main/web_server/web_server.c`, lignes 1302-1315).【F:main/config_manager/config_manager.c†L1290-L1340】【F:main/web_server/web_server.c†L1302-L1315】
- **Impact** : Compromission directe des accès Wi-Fi et MQTT (élévation de privilèges, injection de commandes, usurpation de données télémétriques). Critique sur un système embarqué connecté.
- **Solution proposée** :
  - Supprimer ou restreindre l'endpoint public (authentification forte requise).
  - À minima, filtrer le snapshot envoyé côté HTTP pour remplacer les champs sensibles par des placeholders (comme déjà fait dans `web_server_api_mqtt_config_get_handler`).
  - Stocker et retourner des secrets chiffrés côté flash et n'exposer les valeurs qu'à des clients authentifiés.

```c
// Exemple de masquage avant envoi
config_manager_config_snapshot_t safe = snapshot;
strncpy(safe.wifi.sta.password, "********", sizeof(safe.wifi.sta.password));
strncpy(safe.mqtt.password, "********", sizeof(safe.mqtt.password));
```

## Problèmes élevés

### 2. Mode AP de secours irréversible vers le mode station
- **Description** : Lorsqu'un nombre d'échecs de connexion station est atteint, `wifi_start_ap_mode()` bascule l'ESP32 en `WIFI_MODE_AP` et lance l'AP de secours. Aucune logique ne ramène ensuite la pile Wi-Fi en mode station : l'évènement `IP_EVENT_STA_GOT_IP` ne fait que remettre le flag `s_ap_fallback_active` à `false`. Le module reste en mode point d'accès tant qu'il n'est pas redémarré ou reconfiguré manuellement.
- **Localisation** : Activation de l'AP (`main/wifi/wifi.c`, lignes 240-358) et gestion d'événements (`main/wifi/wifi.c`, lignes 426-434).【F:main/wifi/wifi.c†L240-L358】【F:main/wifi/wifi.c†L426-L434】
- **Impact** : Après une perte temporaire du Wi-Fi, le produit n'essaiera plus jamais de se reconnecter au réseau station. Il reste isolé ou accessible uniquement via l'AP de secours, ce qui coupe la passerelle MQTT/Cloud.
- **Solution proposée** :
  - À la reconnexion ou lorsque de nouvelles credentials sont appliquées, arrêter explicitement l'AP (`esp_wifi_stop()`), repasser en `WIFI_MODE_STA` et relancer `esp_wifi_start()`.
  - Conserver un timer périodique pour réessayer la connexion station même quand l'AP est actif.

```c
if (ap_active && new_sta_credentials) {
    esp_wifi_stop();
    wifi_configure_sta();
    esp_wifi_start();
}
```

### 3. AP de secours ouvert lorsque le mot de passe est court
- **Description** : Si le mot de passe de l'AP est vide ou comporte moins de 8 caractères, le code force `WIFI_AUTH_OPEN`, créant un hotspot non sécurisé par défaut. Les valeurs usine (chaînes vides) mènent donc à un réseau ouvert.
- **Localisation** : `wifi_start_ap_mode()` gère le cas `< 8` caractères en supprimant la protection (`main/wifi/wifi.c`, lignes 221-238).【F:main/wifi/wifi.c†L221-L238】
- **Impact** : Un attaquant proche peut se connecter librement à l'AP de secours, accéder au panneau web, modifier la configuration ou lancer des OTA malveillantes. Risque élevé.
- **Solution proposée** :
  - Bloquer le démarrage de l'AP tant qu'un mot de passe conforme n'est pas configuré.
  - Générer un secret fort par défaut (stocké en NVS) et l'afficher via l'interface physique plutôt que d'ouvrir le réseau.

## Problèmes moyens

### 4. Analyse JSON fragile côté serveur web
- **Description** : Les helpers `web_server_extract_json_string()` et associés parcourent la chaîne à la main en cherchant le prochain `"`. Les séquences échappées (`\"`, `\\`) terminent prématurément l'analyse et tronquent la valeur. Toute configuration contenant un guillemet ou un backslash (ex. chemins Windows, certificats PEM encodés) est donc corrompue ; certaines valeurs peuvent devenir invalides ou déclencher des erreurs de validation downstream.
- **Localisation** : Fonctions d'extraction JSON (`main/web_server/web_server.c`, lignes 1020-1080).【F:main/web_server/web_server.c†L1020-L1080】
- **Impact** : Impossible de saisir des valeurs valides contenant des caractères échappés ; risque de dysfonctionnement silencieux et de perte de configuration.
- **Solution proposée** : Remplacer ces helpers par l'utilisation systématique de `cJSON` (déjà embarqué) pour parser la requête, ce qui gère correctement l'échappement et les types.

### 5. APIs REST sans contrôle d'accès
- **Description** : L'ensemble des handlers HTTP (config, OTA, reboot, historique, etc.) sont exposés sans mécanisme d'authentification ni CSRF token. Couplé au point 1, cela donne un panneau d'administration totalement ouvert.
- **Localisation** : Enregistrement des routes HTTP (`main/web_server/web_server.c`, multiples handlers comme lignes 1302-1540, 1980-2140).【F:main/web_server/web_server.c†L1302-L1540】【F:main/web_server/web_server.c†L1980-L2140】
- **Impact** : Toute personne sur le réseau peut reconfigurer l'appareil, pousser un firmware ou redémarrer le système. Impact majeur sur la sécurité opérationnelle.
- **Solution proposée** :
  - Ajouter une authentification (Basic/Digest, token signé ou mTLS) et des contrôles CSRF.
  - Segmenter les routes sensibles (OTA, config) derrière une autorisation spécifique.

### 6. Gestion concurrente partielle de la configuration
- **Description** : La structure `s_config_json` et divers getters reposent sur un mutex (`s_config_mutex`), mais plusieurs chemins (ex. `config_manager_get_mqtt_client_config`) retournent des pointeurs sur la configuration globale après avoir relâché le verrou. Toute mise à jour concurrente peut modifier les données pendant qu'un module les exploite.
- **Localisation** : Commentaires TODO et prise de lock dans le getter (`main/config_manager/config_manager.c`, lignes 729-748 et 1882-1894).【F:main/config_manager/config_manager.c†L729-L748】【F:main/config_manager/config_manager.c†L1882-L1894】
- **Impact** : Race conditions potentielles lors de publications MQTT ou de snapshots, conduisant à des incohérences (topics/données mixtes) ou à l'utilisation de credentials partiellement mis à jour.
- **Solution proposée** :
  - Fournir des copies thread-safe (structure clonée) plutôt que des pointeurs vers l'état global.
  - Centraliser l'accès via des fonctions qui restent lockées le temps de la copie et documenter la durée de validité.

## Problèmes faibles / amélioration continue

### 7. Construction manuelle de gros JSON
- **Description** : `config_manager_build_config_snapshot_locked()` assemble le JSON à base de `snprintf` chaînés. La lisibilité est faible et le moindre ajout est source d'erreurs de format.
- **Localisation** : `config_manager_build_config_snapshot_locked()` (`main/config_manager/config_manager.c`, autour des lignes 1220-1340).【F:main/config_manager/config_manager.c†L1224-L1340】
- **Impact** : Dette technique, risque de régression lors des évolutions (débordement, guillemets oubliés).
- **Solution proposée** : Utiliser `cJSON` pour construire l'objet (déjà dépendance du module) ou un builder dédié. Cela simplifiera aussi le masquage des secrets.

### 8. Messages d'erreur OTA peu exploitables
- **Description** : En cas d'échec d'upload, le code renvoie simplement un 500 générique sans préciser la cause exacte (taille, CRC, partition).
- **Localisation** : Handler OTA (`main/web_server/web_server.c`, lignes 2049-2140).【F:main/web_server/web_server.c†L2049-L2140】
- **Impact** : Diagnostic difficile côté client, oblige à consulter les logs série.
- **Solution proposée** : Normaliser les réponses JSON avec un champ `error_code` et documenter les cas.

## Performance et maintenabilité
- Revoir la taille des buffers statiques des handlers web : certains (`WEB_SERVER_TASKS_JSON_SIZE = 8 KB`) sont alloués sur la pile des handlers, risquant un stack overflow. Muter ces buffers en allocations statiques/dynamiques ou réduire les snapshots.【F:main/web_server/web_server.c†L54-L82】
- Factoriser la logique de publication MQTT (plusieurs modules construisent des chaînes avec `snprintf` identiques) afin d'éviter la duplication et faciliter les évolutions de format.【F:main/mqtt/tiny_mqtt_publisher.c†L46-L174】

## Résumé exécutif
- **Forces** : Architecture modulaire claire (bus d'évènements, gestion de la configuration, OTA). Bonne couverture des cas d'erreurs ESP-IDF.
- **Faiblesses majeures** : Surface HTTP totalement non protégée et exposition directe des secrets ; modes réseau de secours inachevés ; parsing JSON artisanal fragile.
- **Priorités** : Sécuriser l'API (authentification + masquage des secrets), corriger le retour automatique en mode station, imposer un mot de passe fort pour l'AP et basculer vers `cJSON` pour tous les traitements JSON.

**Note globale** : 4/10 — architecture prometteuse mais la sécurité et la robustesse réseau doivent être traitées en priorité pour un déploiement terrain.
