# Guide du développeur TinyBMS-GW

Ce document fournit toutes les informations nécessaires pour développer, tester et déboguer le firmware TinyBMS-GW.

---

## 📋 Table des matières

1. [Configuration environnement](#configuration-environnement)
2. [Structure du projet](#structure-du-projet)
3. [Build et flash](#build-et-flash)
4. [Debugging](#debugging)
5. [Tests](#tests)
6. [Conventions de code](#conventions-de-code)
7. [Contribution](#contribution)

---

## 🔧 Configuration environnement

### Prérequis

- **OS** : Linux, macOS, ou Windows (WSL2 recommandé)
- **Python** : 3.8+
- **Git** : 2.20+
- **Espace disque** : ~5 GB

### Installation ESP-IDF

```bash
# 1. Cloner ESP-IDF v5.x
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.2.1  # ou version stable

# 2. Installer outils
./install.sh esp32s3

# 3. Configurer environnement (à exécuter dans chaque terminal)
. ~/esp/esp-idf/export.sh

# Optionnel: Ajouter alias à ~/.bashrc
echo 'alias get_idf=". ~/esp/esp-idf/export.sh"' >> ~/.bashrc
```

### Cloner le projet

```bash
git clone https://github.com/thieryfr/TinyBMS-GW.git
cd TinyBMS-GW
```

### Installation dépendances

```bash
# Installer composants ESP-IDF requis
idf.py install
```

---

## 📁 Structure du projet

```
TinyBMS-GW/
├── main/                          # Code source principal
│   ├── app_main.c                 # Point d'entrée application
│   ├── uart_bms/                  # Module UART BMS
│   │   ├── uart_bms.cpp           # Communication TinyBMS
│   │   ├── uart_bms.h
│   │   ├── uart_frame_builder.cpp # Construction trames
│   │   └── uart_response_parser.cpp # Parsing réponses
│   ├── can_victron/               # Module CAN Victron
│   │   ├── can_victron.c          # Driver CAN/TWAI
│   │   └── can_victron.h
│   ├── can_publisher/             # Publication CAN
│   │   ├── can_publisher.c        # Orchestration publication
│   │   └── conversion_table.c     # Conversion BMS→CAN
│   ├── event_bus/                 # Bus événements pub/sub
│   │   ├── event_bus.c
│   │   └── event_bus.h
│   ├── web_server/                # Serveur web HTTP/WS
│   │   ├── web_server.c           # Core serveur
│   │   ├── web_server_alerts.c    # WebSocket alerts
│   │   ├── auth_rate_limit.c      # Rate limiting
│   │   ├── https_config.c         # Configuration HTTPS
│   │   └── certs/                 # Certificats SSL
│   ├── mqtt_client/               # Client MQTT
│   │   ├── mqtt_client.c          # Wrapper esp-mqtt
│   │   ├── mqtts_config.c         # Configuration MQTTS
│   │   └── certs/                 # Certificats MQTT
│   ├── mqtt_gateway/              # Gateway MQTT
│   ├── config_manager/            # Gestion configuration
│   ├── alert_manager/             # Gestion alertes
│   ├── history_logger/            # Logging historique
│   ├── monitoring/                # Métriques système
│   ├── ota_update/                # Mise à jour OTA
│   │   ├── ota_signature.c        # Vérification signature
│   │   └── keys/                  # Clés RSA OTA
│   ├── wifi/                      # Gestion WiFi
│   └── CMakeLists.txt             # Configuration build
├── components/                    # Composants réutilisables
├── test/                          # Tests unitaires
│   └── unity/                     # Framework Unity
├── scripts/                       # Scripts helper
│   └── sign_firmware.sh           # Signature OTA
├── partitions.csv                 # Table partitions flash
├── sdkconfig.defaults             # Configuration par défaut
├── CMakeLists.txt                 # Configuration projet
├── ARCHITECTURE.md                # Documentation architecture
├── DEVELOPMENT.md                 # Ce fichier
├── MODULES.md                     # Référence modules
└── PHASE*.md                      # Documentation phases
```

---

## 🔨 Build et flash

### Configuration initiale

```bash
# Configurer menuconfig (optionnel)
idf.py menuconfig

# Navigation:
# - Component config → TinyBMS-GW
#   - Enable HTTPS (Phase 1)
#   - Enable MQTTS (Phase 2)
#   - Enable OTA signature verification (Phase 1)
```

### Build

```bash
# Build complet
idf.py build

# Build verbeux (debug)
idf.py -v build

# Nettoyage
idf.py fullclean
idf.py build
```

### Flash

```bash
# Flash sur port par défaut
idf.py flash

# Flash sur port spécifique
idf.py -p /dev/ttyUSB0 flash

# Flash + monitor
idf.py flash monitor

# Erase flash complet avant flash
idf.py erase-flash
idf.py flash
```

### Monitoring

```bash
# Démarrer monitor série
idf.py monitor

# Quitter: Ctrl+]

# Filtrer logs par tag
idf.py monitor | grep "uart_bms"
idf.py monitor | grep -E "(ERROR|WARN)"

# Sauvegarder logs
idf.py monitor > logs.txt 2>&1
```

---

## 🐛 Debugging

### Logs ESP-IDF

```c
// Niveaux de log (esp_log.h)
ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));   // Erreur
ESP_LOGW(TAG, "Warning: timeout");                   // Warning
ESP_LOGI(TAG, "Info: connected");                    // Info
ESP_LOGD(TAG, "Debug: variable=%d", value);          // Debug
ESP_LOGV(TAG, "Verbose: detailed trace");            // Verbose

// Configuration niveau par tag (menuconfig)
// Component config → Log output → Default log verbosity
```

**Changer niveau runtime** :
```c
esp_log_level_set("uart_bms", ESP_LOG_DEBUG);
esp_log_level_set("*", ESP_LOG_INFO);  // Tous les tags
```

### GDB debugger

```bash
# Démarrer OpenOCD (terminal 1)
openocd -f board/esp32s3-builtin.cfg

# Démarrer GDB (terminal 2)
xtensa-esp32s3-elf-gdb build/tinybms-gw.elf
(gdb) target remote :3333
(gdb) mon reset halt
(gdb) flushregs
(gdb) thb app_main
(gdb) c
```

**Commandes GDB utiles** :
```gdb
# Breakpoints
break uart_bms.cpp:678
break event_bus_publish if event->id == 0x01

# Execution
continue
step
next
finish

# Inspection
print variable
info threads
bt  # Backtrace
info registers

# Watchpoints
watch s_config.mqtt_broker_uri
```

### Core dump analysis

Configurer dans menuconfig :
```
Component config → ESP System Settings → Panic handler behaviour
  → Core dump to flash
```

Analyser après panic :
```bash
idf.py coredump-info
idf.py coredump-debug
```

### Memory debugging

**Heap tracing** :
```c
#include "esp_heap_trace.h"

// Enable heap tracing
heap_trace_init_standalone(trace_record, NUM_RECORDS);
heap_trace_start(HEAP_TRACE_LEAKS);

// ... code to trace ...

heap_trace_stop();
heap_trace_dump();
```

**Vérifier heap** :
```c
ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free heap: %d bytes", esp_get_minimum_free_heap_size());
```

### Task monitoring

```c
// main/monitoring/monitoring.c:45-78
void monitoring_print_task_stats(void)
{
    char stats_buffer[1024];
    vTaskList(stats_buffer);
    ESP_LOGI(TAG, "Task list:\n%s", stats_buffer);

    vTaskGetRunTimeStats(stats_buffer);
    ESP_LOGI(TAG, "Task runtime:\n%s", stats_buffer);
}
```

Activer dans menuconfig :
```
Component config → FreeRTOS
  → [*] Enable FreeRTOS trace facility
  → [*] Enable FreeRTOS stats formatting functions
```

---

## 🧪 Tests

### Tests unitaires (Unity)

```bash
# Structure
test/
├── unity/                    # Framework Unity
├── test_event_bus.c          # Tests event bus
├── test_config_manager.c     # Tests config
└── CMakeLists.txt

# Build et run
cd test
idf.py build
idf.py flash monitor

# Attendu:
# --------- Unity Test Summary ---------
# Tests: 15  Failures: 0  Ignored: 0
# --------------------------------------
```

**Exemple test** :
```c
#include "unity.h"
#include "event_bus.h"

void test_event_bus_publish_success(void)
{
    event_bus_init();

    event_bus_event_t event = {
        .id = 0x01,
        .payload = NULL,
        .payload_size = 0
    };

    bool result = event_bus_publish(&event, pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE(result);

    event_bus_deinit();
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_event_bus_publish_success);
    UNITY_END();
}
```

### Tests d'intégration

**Test UART → CAN** :
```bash
# 1. Connecter TinyBMS sur UART
# 2. Connecter oscilloscope/analyzer sur CAN
# 3. Monitor logs
idf.py monitor | grep -E "(uart_bms|can_victron)"

# Vérifier:
# - Réception trames UART (toutes les 100ms)
# - Parsing réussi
# - Émission CAN frames (0x351, 0x355...)
```

**Test Web API** :
```bash
# GET config
curl http://192.168.1.100/api/config

# POST config (avec auth)
curl -X POST -u "admin:password" \
  -H "Content-Type: application/json" \
  -d '{"can_enabled":true}' \
  http://192.168.1.100/api/config

# WebSocket alerts
wscat -c ws://192.168.1.100/ws/alerts
```

**Test MQTT** :
```bash
# Subscribe
mosquitto_sub -h broker.example.com -p 8883 \
  --cafile main/mqtt_client/certs/mqtt_ca_cert.pem \
  -t "tinybms/#" -v

# Vérifier publications toutes les secondes
```

### Tests de charge

**Rate limiting** :
```bash
# Attaque brute-force simulée
for i in {1..20}; do
  curl -u "admin:badpass" http://192.168.1.100/api/config
  echo "Attempt $i"
  sleep 0.5
done

# Vérifier lockout après 5 tentatives
# Attendu: HTTP 429 "Too Many Requests"
```

**WebSocket stress** :
```bash
# Ouvrir 10 connexions WS concurrentes
for i in {1..10}; do
  wscat -c ws://192.168.1.100/ws/alerts &
done

# Vérifier stabilité
idf.py monitor | grep "WebSocket"
```

---

## 📝 Conventions de code

### Style C

```c
// Nommage
#define MAX_BUFFER_SIZE 256           // Constantes: UPPER_CASE
typedef struct mqtt_config_t { ... }; // Types: snake_case_t
static int s_counter = 0;             // Statiques: s_ prefix
void event_bus_init(void);            // Fonctions: module_action

// Indentation: 4 espaces (PAS de tabs)
if (condition) {
    do_something();
} else {
    do_other();
}

// Accolades: K&R style
void function(void)
{
    // ...
}

// Commentaires
/* Multi-line comment
 * avec astérisques alignés
 */
// Single line comment

/**
 * @brief Docstring Doxygen
 * @param[in] input Input parameter
 * @param[out] output Output parameter
 * @return ESP_OK on success
 */
```

### Style C++

```cpp
// Nommage (uart_bms.cpp)
class ResponseParser {  // PascalCase
private:
    int m_timeout;      // Membres: m_ prefix

public:
    void parseFrame();  // Méthodes: camelCase
};

// Namespace
namespace tinybms {
    // ...
}

// RAII pour ressources
{
    std::lock_guard<std::mutex> lock(mutex);
    // ... section critique ...
}  // Unlock automatique
```

### Gestion erreurs

```c
// Toujours vérifier esp_err_t
esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
}

// Cleanup approprié
void resource_cleanup(void)
{
    if (handle != NULL) {
        resource_close(handle);
        handle = NULL;
    }
}

// Pas de silent failures
if (buffer == NULL) {
    ESP_LOGE(TAG, "Buffer allocation failed");
    return ESP_ERR_NO_MEM;  // TOUJOURS retourner erreur
}
```

### Thread safety

```c
// TOUJOURS utiliser timeouts
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    // Section critique
    xSemaphoreGive(mutex);
} else {
    ESP_LOGW(TAG, "Mutex timeout");
    return ESP_ERR_TIMEOUT;
}

// Éviter mutex imbriqués
// BAD:
xSemaphoreTake(mutex_a, ...);
xSemaphoreTake(mutex_b, ...);  // Risque deadlock

// GOOD:
// Ordre d'acquisition défini: toujours A puis B
```

### Sécurité

```c
// TOUJOURS utiliser snprintf
char buffer[64];
snprintf(buffer, sizeof(buffer), "Value: %d", value);

// PAS strcpy/strcat
// JAMAIS sprintf

// Vérifier bounds
if (index >= ARRAY_SIZE) {
    ESP_LOGE(TAG, "Index out of bounds: %zu >= %d", index, ARRAY_SIZE);
    return ESP_ERR_INVALID_ARG;
}

// Clear sensitive data
char password[64];
// ... use password ...
memset(password, 0, sizeof(password));  // Zeroize
```

---

## 🤝 Contribution

### Workflow Git

```bash
# 1. Créer branche feature
git checkout -b feature/my-new-feature

# 2. Développer et commit
git add .
git commit -m "Add: feature description"

# 3. Push
git push origin feature/my-new-feature

# 4. Créer Pull Request sur GitHub
```

### Messages de commit

**Format** :
```
<type>: <description courte>

<description détaillée optionnelle>

<footer optionnel: issue refs, breaking changes>
```

**Types** :
- `Add:` Nouvelle fonctionnalité
- `Fix:` Correction bug
- `Refactor:` Refactoring sans changement fonctionnel
- `Docs:` Documentation seulement
- `Test:` Ajout/modification tests
- `Perf:` Amélioration performance
- `Security:` Correction vulnérabilité

**Exemples** :
```
Add: UART interrupt-driven mode

Replace polling with event queue for 67% latency reduction.
- New uart_event_task() with UART_EVENT_QUEUE_SIZE=20
- Handle UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL
- Configurable via CONFIG_TINYBMS_UART_EVENT_DRIVEN

Closes #42
```

```
Fix: Race condition in can_victron_deinit()

Use thread-safe helper can_victron_is_driver_started()
instead of direct s_driver_started access.

Fixes BUG-002 from security audit.
```

### Checklist PR

Avant de soumettre une Pull Request, vérifier :

- [ ] Code compile sans warnings (`idf.py build`)
- [ ] Tests unitaires passent (si applicable)
- [ ] Tests d'intégration effectués
- [ ] Documentation mise à jour (si API publique modifiée)
- [ ] Pas de secrets/credentials commitées
- [ ] Code formaté selon conventions
- [ ] Logs appropriés ajoutés (ESP_LOGI/W/E)
- [ ] Thread-safety vérifiée (mutexes, timeouts)
- [ ] Sécurité vérifiée (buffer bounds, input validation)
- [ ] Pas de régression fonctionnelle

### Revue de code

**Points à vérifier** :

1. **Fonctionnalité** : Le code fait-il ce qu'il doit faire ?
2. **Sécurité** : Vulnérabilités potentielles ?
3. **Performance** : Goulots d'étranglement ?
4. **Maintenabilité** : Code lisible et documenté ?
5. **Tests** : Couverture suffisante ?

---

## 🔍 Troubleshooting

### Problèmes courants

**"Flash size too small"** :
```bash
# Vérifier partition table
idf.py partition-table

# Augmenter taille flash dans menuconfig
# Serial flasher config → Flash size → 8 MB
```

**"No serial ports found"** :
```bash
# Linux: Permissions
sudo usermod -a -G dialout $USER
# Logout/login

# Vérifier port
ls /dev/ttyUSB*
ls /dev/ttyACM*

# Mac:
ls /dev/cu.usbserial*
```

**"MQTT connection failed"** :
```bash
# Vérifier certificats
ls -l main/mqtt_client/certs/
# Doit contenir mqtt_ca_cert.pem

# Test connexion broker
mosquitto_sub -h broker.example.com -p 8883 \
  --cafile main/mqtt_client/certs/mqtt_ca_cert.pem \
  -t "test" -v
```

**"OTA update rejected"** :
```bash
# Vérifier signature
./scripts/sign_firmware.sh build/tinybms-gw.bin

# Vérifier clé publique embarquée
ls main/ota_update/keys/ota_public_key.pem
```

**"Out of memory"** :
```c
// Vérifier heap
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());

// Identifier leaks
idf.py monitor | grep "CORRUPT HEAP"

# Réduire stack tasks si nécessaire (CMakeLists.txt)
```

---

## 📚 Ressources

### Documentation ESP-IDF

- **API Reference** : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/
- **Get Started** : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/
- **Best Practices** : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/
- **FreeRTOS** : https://www.freertos.org/Documentation/

### Outils

- **VS Code ESP-IDF Extension** : https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension
- **PlatformIO** : https://platformio.org/
- **esptool** : https://github.com/espressif/esptool

### Communauté

- **ESP32 Forum** : https://esp32.com/
- **GitHub Issues** : https://github.com/thieryfr/TinyBMS-GW/issues
- **ESP-IDF GitHub** : https://github.com/espressif/esp-idf

---

## ⚙️ Configuration avancée

### Partitions personnalisées

Éditer `partitions.csv` :
```csv
# Name,   Type, SubType,  Offset,  Size,    Flags
nvs,      data, nvs,      0x9000,  0x6000,
phy_init, data, phy,      0xf000,  0x1000,
factory,  app,  factory,  0x10000, 1M,
storage,  data, spiffs,   0x110000, 512K,
ota_0,    app,  ota_0,    0x210000, 1536K,
ota_1,    app,  ota_1,    0x390000, 1536K,
```

Appliquer :
```bash
idf.py partition-table-flash
```

### Optimisation taille binaire

```bash
# Menuconfig
# Compiler options → Optimization Level → Optimize for size (-Os)
# Component config → LWIP → Enable LWIP IRAM optimization
# Component config → FreeRTOS → Place FreeRTOS functions into flash

# Build
idf.py size-components
idf.py size-files
```

### Sécurité production

**Secure Boot** :
```bash
# Générer clés
espsecure.py generate_signing_key secure_boot_signing_key.pem

# Activer dans menuconfig
# Security features → [*] Enable hardware Secure Boot in bootloader

# Build et flash
idf.py bootloader
idf.py flash
```

**Flash Encryption** :
```bash
# Menuconfig
# Security features → [*] Enable flash encryption on boot

# Une seule fois !
idf.py encrypted-flash
```

⚠️ **ATTENTION** : Secure Boot et Flash Encryption sont IRRÉVERSIBLES sur certains ESP32. Tester sur device de dev d'abord.

---

**Version** : 1.0 (Phase 3)
**Dernière mise à jour** : 2025-01-17
