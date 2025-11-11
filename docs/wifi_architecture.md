# Architecture du module Wi-Fi

La pile Wi-Fi a été scindée en plusieurs modules afin de clarifier les responsabilités
et de faciliter les tests unitaires :

- `wifi_state.h` / `wifi_state.c` définissent la structure d'état partagée et la gestion
  des ressources synchrones (mutex, timer, handles ESP-IDF).
- `wifi_events.h` / `wifi_events.c` encapsulent la publication des événements
  applicatifs sur l'`event_bus` avec un pool de métadonnées circulaire.
- `wifi_state_machine.h` / `wifi_state_machine.c` implémentent la logique de
  transitions STA⇄AP, les tentatives de reconnexion et le basculement AP
  de secours.
- `wifi.c` fournit l'interface publique (`wifi_init`, `wifi_deinit`,
  `wifi_start_sta_mode`) et orchestre l'initialisation de la pile ESP-IDF.

## Structure d'état partagée

`wifi_shared_state_t` regroupe les indicateurs runtime (initialisation, nombre de
retries, activation de l'AP de secours) ainsi que les ressources dépendantes de la
plateforme :

- mutex FreeRTOS protégeant l'état mutable ;
- timer de retry STA (`sta_retry_timer`) ;
- handles d'abonnement aux événements ESP-IDF ;
- pointeurs `esp_netif_t` des interfaces STA et AP ;
- hook `event_bus_publish_fn_t` pour propager les événements applicatifs.

Cette structure est passée à tous les modules afin d'éviter les variables globales
non synchronisées et centraliser la synchronisation.

## Transitions STA⇄AP

La fonction `wifi_state_machine_process_transition()` reçoit des transitions
`wifi_state_transition_t` issues du handler ESP-IDF et applique les actions
associées :

- `WIFI_STATE_TRANSITION_STA_START` lance une connexion STA et émet
  `APP_EVENT_ID_WIFI_STA_START`.
- `WIFI_STATE_TRANSITION_STA_CONNECTED` réinitialise les compteurs de retry et
  publie `APP_EVENT_ID_WIFI_STA_CONNECTED`.
- `WIFI_STATE_TRANSITION_STA_DISCONNECTED` incrémente les retries, déclenche
  `esp_wifi_connect()` ou bascule vers l'AP de secours après épuisement des essais.
- `WIFI_STATE_TRANSITION_STA_GOT_IP` arrête le timer de retry et confirme la
  connexion via `APP_EVENT_ID_WIFI_STA_GOT_IP`.
- Les transitions AP (`*_AP_*`) notifient l'état du point d'accès et reschedulent
  le timer STA lorsque l'AP de secours est actif.

## Tests unitaires

Le fichier `test/test_wifi_state_machine.c` simule les transitions ESP-IDF en
appelant `wifi_state_machine_process_transition()` avec des événements
représentatifs. Les tests vérifient la publication des événements `APP_EVENT_ID_*`
associés et servent de garde-fou lors de futures évolutions de la machine
d'états.

