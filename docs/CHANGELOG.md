# Changelog

## [Unreleased]
### Added
- Exposition d'un état interne `mqtt_client_state_t` et API `mqtt_client_get_state` pour faciliter le diagnostic du client MQTT sur cible et en tests unitaires.【F:main/mqtt_client/mqtt_client.h†L44-L130】【F:main/mqtt_client/mqtt_client.c†L309-L330】
- Suite de tests Unity couvrant l'initialisation, la configuration, la publication et l'enregistrement de callbacks du client MQTT.【F:test/test_mqtt_client.c†L1-L129】
- Documentation détaillée de la configuration, des topics et des scénarios de tests du module MQTT, y compris des recommandations de validation manuelle via Mosquitto.【F:docs/mqtt_module.md†L1-L71】

### Changed
- Mise à jour de la configuration des tests pour intégrer la nouvelle batterie de tests MQTT et le composant `mqtt_client` comme dépendance explicite.【F:test/CMakeLists.txt†L1-L3】
