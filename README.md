# TinyBMS Web Gateway

Squelette de projet ESP-IDF pour la passerelle TinyBMS ↔ Victron avec interface web embarquée. Cette arborescence prépare l'intégration des différents modules (UART BMS, CAN Victron, MQTT, monitoring, etc.) ainsi que la partie front-end servie depuis l'ESP32.

## Structure du projet
```
TinyBMS-WebGateway/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── include/
│   │   └── app_config.h
│   ├── event_bus/
│   ├── uart_bms/
│   ├── can_victron/
│   ├── pgn_mapper/
│   ├── web_server/
│   ├── config_manager/
│   ├── mqtt_client/
│   └── monitoring/
├── web/
├── test/
├── docs/
├── .gitignore
├── README.md
└── idf_component.yml
```

Chaque sous-répertoire `main/<module>` contient un `CMakeLists.txt` dédié et des stubs C/C++ prêts à être complétés.

## Premiers pas
1. Installer l'ESP-IDF v5.x.
2. Configurer le projet :
   ```bash
   idf.py set-target esp32
   idf.py menuconfig
   ```
3. Compiler et flasher :
   ```bash
   idf.py build
   idf.py flash monitor
   ```

## Documentation
- `docs/architecture.md` : description de l'architecture logicielle.
- `docs/pgn_mapping.xlsx` : table de correspondance PGN (à compléter).
- `docs/api_endpoints.md` : documentation des endpoints REST/WebSocket.

## Interface web
Les assets statiques sont disponibles dans `web/`. Ils seront intégrés dans une partition SPIFFS et servis via le module `web_server`.
