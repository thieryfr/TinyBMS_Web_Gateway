# Module `event_bus`

## Références
- `main/event_bus/event_bus.h`
- `main/event_bus/event_bus.c`
- `main/include/app_events.h`

## Diagramme UML
```mermaid
classDiagram
    class EventBus {
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
