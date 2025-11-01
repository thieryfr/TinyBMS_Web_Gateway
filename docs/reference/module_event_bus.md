# Module `event_bus`

## Références
- `main/event_bus/event_bus.h`
- `main/event_bus/event_bus.c`
- `main/include/app_events.h`

## Diagramme UML
```mermaid
classDiagram
    class EventBus {
        +init()
        +deinit()
        +subscribe(queue_length, callback, context)
        +unsubscribe(handle)
        +publish(event, timeout)
        +receive(handle, event, timeout)
        +dispatch(handle, timeout)
        +void init()
        +void deinit()
        +event_bus_subscription_handle_t subscribe()
        +void unsubscribe(handle)
        +bool publish(event, timeout)
        +bool receive(handle, event, timeout)
        +bool dispatch(handle, timeout)
    }
    class Subscription {
        QueueHandle_t queue
        callback
        context
        next
    }
    EventBus o-- Subscription
    EventBus <.. "Producteurs" : publish()
    EventBus <.. "Consommateurs" : subscribe()/dispatch()
```

## Rôle et responsabilités
Le bus d'évènements fournit une infrastructure de publication/abonnement thread-safe bâtie sur FreeRTOS. Chaque module peut diffuser des notifications asynchrones sans connaître ses consommateurs, tandis que les abonnés disposent d’une queue dédiée et, optionnellement, d’un callback pour le traitement immédiat.【F:main/event_bus/event_bus.c†L1-L154】

## API et paramètres
- `event_bus_subscribe(queue_length, callback, context)` : crée une queue (`xQueueCreate`) de `queue_length` éléments (taille recommandée ≥ 4 pour éviter les pertes) et l’insère dans la liste chaînée protégée par `s_bus_lock`.
- `event_bus_publish(event, timeout)` : parcourt la liste, envoie le message sur chaque queue (timeout `TickType_t`). Retourne `false` si au moins une queue est pleine ou si le bus n’est pas initialisé.【F:main/event_bus/event_bus.c†L61-L119】
- `event_bus_receive()` / `event_bus_dispatch()` : lecture bloquante et exécution du callback utilisateur.
- `event_bus_deinit()` : libère toutes les queues, détruit le mutex et vide la liste ; utile pour les tests ou un redémarrage contrôlé.【F:main/event_bus/event_bus.c†L31-L59】

## Synchronisation
- `s_bus_lock` (mutex FreeRTOS) sérialise la modification/itération sur la liste d’abonnés.
- `s_init_spinlock` empêche les courses lors de la création paresseuse du mutex pendant `event_bus_init()`.
- Les payloads référencés dans `event_bus_event_t` restent la propriété du producteur ; de nombreux modules copient les données dans un buffer circulaire avant publication pour garantir leur durée de vie.【F:main/event_bus/event_bus.c†L15-L119】

## Tableau des évènements applicatifs
| Identifiant | Producteur principal | Payload | Consommateurs typiques |
| --- | --- | --- | --- |
| `0x1000` (`APP_EVENT_ID_TELEMETRY_SAMPLE`) | `monitoring` | JSON batterie (snapshot) | `web_server` (WebSocket), `mqtt_gateway` (topic status)【F:main/monitoring/monitoring.c†L200-L240】【F:main/mqtt_gateway/mqtt_gateway.c†L200-L254】
| `0x1001` (`APP_EVENT_ID_UI_NOTIFICATION`) | `web_server` | Message JSON (niveau UI) | Clients WebSocket (flux `events`)【F:main/web_server/web_server.c†L720-L908】
| `0x1002` (`APP_EVENT_ID_CONFIG_UPDATED`) | `config_manager` | JSON `{type:...}` | `web_server`, `mqtt_gateway` pour recharger topics/config【F:main/config_manager/config_manager.c†L552-L612】【F:main/mqtt_gateway/mqtt_gateway.c†L200-L254】
| `0x1003` (`APP_EVENT_ID_OTA_UPLOAD_READY`) | `web_server` | Chemin OTA / métadonnées | Service OTA (non implémenté)【F:main/web_server/web_server.c†L1050-L1185】
| `0x1100` (`APP_EVENT_ID_BMS_LIVE_DATA`) | `uart_bms` | `uart_bms_live_data_t` | `monitoring`, `can_publisher`, `pgn_mapper`【F:main/uart_bms/uart_bms.cpp†L558-L607】【F:main/monitoring/monitoring.c†L150-L200】
| `0x1101` (`APP_EVENT_ID_UART_FRAME_RAW`) | `uart_bms` | JSON hex | `web_server` (WS UART), `mqtt_gateway` (topic can/raw)【F:main/uart_bms/uart_bms.cpp†L558-L607】【F:main/web_server/web_server.c†L960-L1058】
| `0x1102` (`APP_EVENT_ID_UART_FRAME_DECODED`) | `uart_bms` | JSON décodé | `web_server`, outils de debug【F:main/uart_bms/uart_bms.cpp†L558-L607】
| `0x1200` (`APP_EVENT_ID_CAN_FRAME_RAW`) | `can_victron` (RX) | JSON hex | `web_server`, `mqtt_gateway` (topic can/raw)【F:main/can_victron/can_victron.c†L74-L220】【F:main/mqtt_gateway/mqtt_gateway.c†L200-L254】
| `0x1201` (`APP_EVENT_ID_CAN_FRAME_DECODED`) | `can_victron` | JSON interprété | `web_server` (WS CAN decoded), `mqtt_gateway` (topic can/decoded)【F:main/can_victron/can_victron.c†L74-L220】
| `0x1202` (`APP_EVENT_ID_CAN_FRAME_READY`) | `can_publisher` | `can_publisher_frame_t` | `mqtt_gateway` (topic can/ready), `web_server` (WS CAN TX)【F:main/can_publisher/can_publisher.c†L104-L205】【F:main/mqtt_gateway/mqtt_gateway.c†L200-L254】
| `0x1300`–`0x1304` (`APP_EVENT_ID_WIFI_*`) | `wifi` | Aucun payload | `mqtt_gateway` (gestion connexion), `web_server` (UI)【F:main/wifi/wifi.c†L1-L220】【F:main/mqtt_gateway/mqtt_gateway.c†L200-L254】
| `0x1310`–`0x1313` (Wi-Fi AP) | `wifi` | Aucun payload | UI WebSocket pour diagnostics AP【F:main/wifi/wifi.c†L1-L260】【F:main/web_server/web_server.c†L720-L908】

## Cas d'utilisation
- `uart_bms` diffuse les trames TinyBMS vers `monitoring`, `can_publisher` et la couche MQTT/WebSocket.
- `mqtt_gateway` s’abonne à un bouquet d’évènements pour publier sur les topics dynamiques définis par `config_manager`.
- `web_server` maintient plusieurs abonnements (télémétrie, évènements UI, flux CAN/UART) pour alimenter les websockets clients.

## Bonnes pratiques
- Choisir un `queue_length` adapté au rythme de publication attendu (ex. 8 pour `mqtt_gateway` qui consomme plusieurs flux rapides).
- Toujours documenter la durée de vie du payload : si un buffer local est utilisé, en publier une copie stable avant de publier.
- Utiliser des timeouts de publication modestes (`25–50 ms`) afin de ne pas bloquer le producteur en cas de consommateur lent.【F:main/event_bus/event_bus.c†L61-L119】

## Extensibilité
1. Ajouter l’identifiant dans `app_events.h` (en respectant la plage hexadécimale de son domaine).
2. Publier `event_bus_event_t` depuis le module producteur avec un schéma de payload documenté.
3. Mettre à jour la présente table pour référencer les nouveaux flux et faciliter l’intégration des consommateurs.
    EventBus <.. "Modules producteurs" : publish()
    EventBus <.. "Modules consommateurs" : subscribe()/receive()
```

## Rôle et responsabilités
Le bus d'évènements fournit une infrastructure de publication/abonnement légère et thread-safe au-dessus des primitives FreeRTOS. Il garantit que chaque module peut diffuser des notifications asynchrones sans connaissance des consommateurs, tout en offrant une isolation via des files de messages dédiées à chaque abonnement.

## Structures de données clés
- `event_bus_event_t` : conteneur transportant l'identifiant d'évènement (`event_bus_event_id_t`), un pointeur vers la charge utile (non possédée par le bus) et sa taille.
- `event_bus_subscription_t` : maillon d'une liste chaînée protégée par un mutex, regroupant la queue FreeRTOS, le callback optionnel et le contexte utilisateur.

## Cycle de vie
1. `event_bus_init()` crée paresseusement le mutex global `s_bus_lock`. L'appel est idempotent.
2. `event_bus_subscribe()` crée une queue circulaire (`xQueueCreate`) et l'enregistre dans la liste protégée. Le callback fourni sera utilisé par `event_bus_dispatch()` pour combiner réception et traitement.
3. `event_bus_publish()` traverse la liste des abonnés et `xQueueSend` chaque message avec le timeout indiqué. La fonction reste atomique grâce au verrou global.
4. `event_bus_receive()` et `event_bus_dispatch()` permettent aux consommateurs de bloquer en attente d'un message.
5. `event_bus_unsubscribe()` supprime un abonné et libère sa queue.
6. `event_bus_deinit()` vide la liste, détruit les files et libère le mutex.

## Concurrence
- `s_bus_lock` assure que la liste des abonnés ne soit jamais parcourue ou modifiée simultanément. Les queues individuelles gèrent l'arbitrage producteur-consommateur.
- Un spinlock (`s_init_spinlock`) protège la création paresseuse du mutex lors d'appels concurrentiels à `event_bus_init()`.
- Les ressources allouées pour chaque publication (payload) restent la responsabilité du module émetteur; l'évènement transporte uniquement un pointeur.

## Identification des évènements
Les identifiants sont centralisés dans `app_events.h`. Ils couvrent les domaines TinyBMS (UART), CAN Victron, Wi-Fi, MQTT, notifications UI, etc. Les valeurs sont choisies dans différentes plages hexadécimales pour faciliter le filtrage.

## Cas d'utilisation
- **Diffusion UART** : `uart_bms` publie des trames brutes/décodées et des échantillons BMS.
- **Passerelle MQTT** : `mqtt_gateway` consomme plusieurs évènements (`APP_EVENT_ID_TELEMETRY_SAMPLE`, `APP_EVENT_ID_CAN_FRAME_READY`, etc.) pour les publier sur différents topics.
- **Websocket** : `web_server` s'abonne aux flux pour pousser des notifications WebSocket.

## Bonnes pratiques
- Chaque module doit limiter la taille des payloads ou les copier dans des buffers circulaires internes avant publication afin d'éviter l'invalidation de pointeurs.
- Les abonnés doivent dé-queue régulièrement pour éviter de saturer leur file (ce qui ferait échouer les publications futures).
- Les timeouts doivent rester brefs (typiquement `pdMS_TO_TICKS(25-50)`) pour ne pas bloquer les producteurs.

## Extensibilité
Pour introduire un nouveau type d'évènement :
1. Ajouter un identifiant dans `app_events.h`.
2. Publier un `event_bus_event_t` en choisissant un schéma de payload (structure C, JSON, string...).
3. Documenter la durée de vie du payload (copié vs prêté).
4. Fournir des utilitaires d'abonnement dans le module producteur si nécessaire.
