# TinyBMS-GW Interface Web

Interface web moderne pour la gestion et le monitoring du TinyBMS Gateway.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)]()
[![Version](https://img.shields.io/badge/version-3.0.0-green.svg)]()

---

## 📋 Table des Matières

- [Aperçu](#aperçu)
- [Fonctionnalités](#fonctionnalités)
- [Architecture](#architecture)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Modules UX](#modules-ux)
- [API Reference](#api-reference)
- [Développement](#développement)
- [Tests](#tests)
- [Déploiement](#déploiement)
- [Contribution](#contribution)

---

## 🎯 Aperçu

L'interface web TinyBMS-GW est une application monopage (SPA) moderne qui permet de:

- **Monitorer** en temps réel l'état de la batterie
- **Configurer** les paramètres MQTT, WiFi, CAN, UART
- **Gérer** les alertes et notifications
- **Visualiser** l'historique des données avec graphiques
- **Contrôler** le TinyBMS à distance

### Technologies Utilisées

- **Frontend:** Vanilla JavaScript (ES6+ modules)
- **CSS:** Tabler CSS Framework
- **Icons:** Tabler Icons
- **Charts:** ECharts (lazy loaded)
- **PWA:** Service Worker pour mode offline
- **i18n:** Support FR + EN

---

## ✨ Fonctionnalités

### Core Features

- ✅ **Dashboard temps réel** - Monitoring batterie avec WebSocket
- ✅ **Configuration complète** - MQTT, WiFi, CAN, UART
- ✅ **Gestion alertes** - Système d'alertes configurable
- ✅ **Historique** - Stockage et visualisation données
- ✅ **Mode sombre** - Dark mode avec détection système
- ✅ **Offline mode** - Application utilisable sans connexion
- ✅ **Multilingue** - Support FR 🇫🇷 + EN 🇬🇧

### UX/Performance (Phase 3)

- 🎨 **Notifications toast** - Queue, animations, actions
- ⏳ **Loading states** - Spinners, skeleton screens
- 🌓 **Theme dynamique** - Light/Dark/Auto avec persistance
- 🌍 **i18n** - Internationalisation FR + EN
- 📡 **Service Worker** - Cache intelligent, offline support
- ⚡ **Lazy loading** - Chargement à la demande des modules

### Developer Experience (Phase 4)

- 📝 **Logging structuré** - Console, storage, export
- 📚 **Documentation complète** - JSDoc, guides, exemples
- 🧪 **Tests** - Units tests ready (Jest config)
- 🔧 **Dev tools** - Logger export, debug mode

---

## 🏗️ Architecture

### Structure des Fichiers

```
web/
├── index.html              # Page principale
├── dashboard.html          # Dashboard batterie
├── config.html             # Configuration
├── alerts.html             # Gestion alertes
├── service-worker.js       # Service Worker (offline)
├── INTEGRATION_GUIDE.md    # Guide développeur
├── README.md              # Ce fichier
│
├── src/
│   ├── css/
│   │   ├── tabler.min.css
│   │   └── tabler-icons.min.css
│   │
│   └── js/
│       ├── tabler.min.js
│       ├── dashboard.js      # Dashboard controller
│       ├── app.js            # Application entry point
│       │
│       ├── components/       # Components UI
│       │   ├── alerts/
│       │   ├── charts/
│       │   └── config/
│       │
│       ├── lib/              # Bibliothèques externes
│       │   ├── echarts.min.js (lazy loaded)
│       │   └── moment.min.js (lazy loaded)
│       │
│       └── utils/            # Modules utilitaires
│           ├── notifications.js   # Toast system
│           ├── loading.js         # Loading states
│           ├── theme.js           # Dark mode
│           ├── i18n.js            # i18n FR+EN
│           ├── offline.js         # Service Worker
│           ├── lazy.js            # Lazy loading
│           └── logger.js          # Logging
│
└── test/
    └── unit/                # Tests unitaires
```

### Flux de Données

```
┌─────────────────┐
│   Browser       │
│   (SPA)         │
└────────┬────────┘
         │
         │ HTTP/WebSocket
         ▼
┌─────────────────┐
│   ESP32         │
│   Web Server    │
│   (C/C++)       │
└────────┬────────┘
         │
         │ Event Bus
         ▼
┌─────────────────────────────────┐
│   Modules Backend               │
├─────────────────────────────────┤
│ • MQTT Client                   │
│ • UART BMS                      │
│ • CAN Publisher                 │
│ • Alert Manager                 │
│ • Config Manager                │
│ • Monitoring                    │
└─────────────────────────────────┘
```

### API Endpoints

**REST API:**
- `GET /api/status` - État système
- `GET /api/config` - Configuration
- `POST /api/config` - Sauvegarder config
- `GET /api/mqtt/config` - Config MQTT
- `GET /api/alerts/active` - Alertes actives
- `GET /api/alerts/history` - Historique alertes
- `POST /api/alerts/acknowledge` - Acquitter alerte

**WebSocket Streams:**
- `ws://host/ws/telemetry` - Données batterie temps réel
- `ws://host/ws/events` - Événements système
- `ws://host/ws/uart` - Trames UART
- `ws://host/ws/can` - Trames CAN
- `ws://host/ws/alerts` - Alertes temps réel

Voir [API Reference](#api-reference) pour documentation complète.

---

## 🚀 Installation

### Prérequis

- **ESP32** avec ESP-IDF v4.4+
- **Partition SPIFFS** pour héberger fichiers web
- **Connexion WiFi** configurée

### Installation sur ESP32

1. **Build firmware avec web server:**

```bash
cd main/web_server
idf.py build
```

2. **Upload fichiers web sur SPIFFS:**

```bash
# Créer image SPIFFS
python $IDF_PATH/components/spiffs/spiffsgen.py \
  1048576 web spiffs.bin

# Flash SPIFFS
esptool.py --chip esp32 --port /dev/ttyUSB0 \
  write_flash 0x310000 spiffs.bin
```

3. **Flash firmware:**

```bash
idf.py flash monitor
```

4. **Accéder interface:**

Ouvrir navigateur: `http://<ESP32_IP>/`

### Installation Développement Local

Pour tester localement sans ESP32:

```bash
# Installer serveur HTTP simple
npm install -g http-server

# Lancer serveur dans dossier web/
cd web
http-server -p 8080

# Ouvrir http://localhost:8080
```

**Note:** API calls échoueront sans backend ESP32.

---

## 💻 Utilisation

### Accès Initial

1. Connecter ESP32 au réseau WiFi
2. Trouver adresse IP (voir logs série ou DHCP)
3. Ouvrir navigateur: `http://<ESP32_IP>/`

### Navigation

- **Dashboard** - Vue d'ensemble batterie
- **Configuration** - Paramètres système
- **Alertes** - Gestion alertes et historique
- **Logs** - Événements système (si activés)

### Configuration WiFi

```
Configuration → WiFi
├── SSID: Nom réseau
├── Password: Mot de passe
├── Static IP: (optionnel)
└── Save → Redémarrage automatique
```

### Configuration MQTT

```
Configuration → MQTT
├── Broker: mqtt://broker.example.com
├── Port: 1883
├── Username/Password: (si requis)
├── Topics: Personnaliser topics
└── Save → Reconnexion automatique
```

### Gestion Alertes

```
Alertes → Configuration
├── Activer alertes
├── Seuils température (min/max)
├── Seuils tension cellules
├── Débounce (éviter spam)
└── Save
```

### Mode Offline

L'application fonctionne hors ligne grâce au Service Worker:

1. Charger page **une fois online** (cache initial)
2. Activer **mode avion** ou perdre connexion
3. Application reste **fonctionnelle** avec données cachées
4. Retour online → **sync automatique**

---

## 🎨 Modules UX

### 1. Notifications

Système toast avec queue et actions.

```javascript
import { notifySuccess, notifyError } from '/src/js/utils/notifications.js';

notifySuccess('Configuration enregistrée');
notifyError('Connexion échouée');
```

[Documentation complète →](INTEGRATION_GUIDE.md#1-notifications)

### 2. Loading States

Spinners et skeleton screens.

```javascript
import { showSpinner, hideSpinner } from '/src/js/utils/loading.js';

const id = showSpinner('#content');
await loadData();
hideSpinner(id);
```

[Documentation complète →](INTEGRATION_GUIDE.md#2-loading-states)

### 3. Theme (Dark Mode)

Mode sombre avec détection système.

```javascript
import { initializeTheme } from '/src/js/utils/theme.js';

initializeTheme({ defaultTheme: 'auto' });
```

[Documentation complète →](INTEGRATION_GUIDE.md#3-theme-dark-mode)

### 4. i18n

Support multilingue FR + EN.

```javascript
import { t, setLanguage } from '/src/js/utils/i18n.js';

console.log(t('common.save')); // "Enregistrer" ou "Save"
setLanguage('en');
```

[Documentation complète →](INTEGRATION_GUIDE.md#4-internationalisation-i18n)

### 5. Offline Mode

Service Worker pour mode hors ligne.

```javascript
import { initializeOfflineMode } from '/src/js/utils/offline.js';

await initializeOfflineMode({ showIndicator: true });
```

[Documentation complète →](INTEGRATION_GUIDE.md#5-offline-mode)

### 6. Lazy Loading

Chargement à la demande.

```javascript
import { lazyLoadModule } from '/src/js/utils/lazy.js';

const echarts = await lazyLoadModule('/src/js/lib/echarts.min.js');
```

[Documentation complète →](INTEGRATION_GUIDE.md#6-lazy-loading)

### 7. Logger

Logging structuré avec export.

```javascript
import { info, error, configure } from '/src/js/utils/logger.js';

configure({ level: 'DEBUG', enableStorage: true });
info('Application started');
```

[Documentation complète →](INTEGRATION_GUIDE.md#7-logger)

---

## 📚 API Reference

### REST API

#### GET /api/status

Retourne état système complet.

**Response:**
```json
{
  "uptime_ms": 123456,
  "free_heap": 45678,
  "wifi": {
    "connected": true,
    "ssid": "MyNetwork",
    "rssi": -45,
    "ip": "192.168.1.100"
  },
  "mqtt": {
    "connected": true,
    "broker": "mqtt://broker.com"
  },
  "battery": {
    "voltage_mv": 52000,
    "current_ma": -1500,
    "soc_percent": 75,
    "temperature_c": 25.5,
    "cells": 16
  }
}
```

#### POST /api/config

Sauvegarde configuration.

**Request:**
```json
{
  "mqtt_broker": "mqtt://192.168.1.10",
  "mqtt_port": 1883,
  "wifi_ssid": "MyNetwork",
  "wifi_password": "secret123"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Configuration saved"
}
```

### WebSocket API

#### ws://host/ws/telemetry

Stream données batterie.

**Messages:**
```json
{
  "type": "battery_data",
  "timestamp_ms": 1234567890,
  "voltage_mv": 52000,
  "current_ma": -1500,
  "soc_percent": 75,
  "temperature_c": 25.5,
  "cells": [
    { "index": 0, "voltage_mv": 3250 },
    { "index": 1, "voltage_mv": 3248 },
    ...
  ]
}
```

**Fréquence:** ~1 Hz (configurable)

[Documentation API complète →](/docs/API.md)

---

## 🛠️ Développement

### Setup Environnement

```bash
# Clone repository
git clone https://github.com/thieryfr/TinyBMS-GW.git
cd TinyBMS-GW/web

# Installer dépendances dev (optionnel)
npm install

# Lancer serveur dev
npm run dev  # ou http-server
```

### Structure Code

**Modules ES6:**
Tous les fichiers JavaScript utilisent `type="module"`:

```html
<script type="module" src="/src/js/app.js"></script>
```

**Imports:**
```javascript
// Named imports
import { notifySuccess, notifyError } from './utils/notifications.js';

// Default import
import logger from './utils/logger.js';
```

### Conventions

**Naming:**
- Fichiers: `kebab-case.js`
- Functions: `camelCase()`
- Classes: `PascalCase`
- Constants: `UPPER_SNAKE_CASE`

**Comments:**
```javascript
/**
 * Function description
 * @param {string} param1 - Description
 * @returns {Promise<Object>} Description
 */
async function myFunction(param1) {
  // Implementation
}
```

### Debugging

**Logger:**
```javascript
import { configure, debug, info } from './utils/logger.js';

// Enable debug mode
configure({ level: 'DEBUG', enableStorage: true });

debug('Detailed info', { data: {...} });

// Export logs
import { downloadLogs } from './utils/logger.js';
downloadLogs('json');
```

**Browser DevTools:**
- Console: Tous les logs structurés
- Network: WebSocket frames
- Application → Service Workers
- Application → Local Storage

---

## 🧪 Tests

### Tests Unitaires

**Framework:** Jest (config prête)

```bash
# Installer Jest
npm install --save-dev jest @jest/globals

# Lancer tests
npm test

# Coverage
npm test -- --coverage
```

**Exemple test:**
```javascript
import { describe, test, expect } from '@jest/globals';
import { t, setLanguage } from '../src/js/utils/i18n.js';

describe('i18n', () => {
  test('should translate correctly', () => {
    setLanguage('fr');
    expect(t('common.save')).toBe('Enregistrer');

    setLanguage('en');
    expect(t('common.save')).toBe('Save');
  });
});
```

### Tests Manuels

**Checklist:**
- [ ] Dashboard affiche données batterie
- [ ] WebSocket reconnecte si déconnecté
- [ ] Configuration sauvegarde correctement
- [ ] Alertes s'affichent et s'acquittent
- [ ] Dark mode fonctionne
- [ ] Changement langue met à jour UI
- [ ] Mode offline charge depuis cache
- [ ] Service Worker update notifie

---

## 📦 Déploiement

### Build Production

```bash
# Minifier JavaScript (optionnel)
npm run build

# Minifier CSS (déjà fait avec Tabler)

# Optimiser images
npm run optimize-images
```

### Upload sur ESP32

```bash
# Générer image SPIFFS
python $IDF_PATH/components/spiffs/spiffsgen.py \
  1048576 web build/spiffs.bin

# Flash
esptool.py --chip esp32 --port /dev/ttyUSB0 \
  write_flash 0x310000 build/spiffs.bin
```

### Configuration Production

**app.js:**
```javascript
// Production config
configure({
  level: 'INFO',  // Pas DEBUG en prod
  enableStorage: false,  // Pas de logs stockés
  enableConsole: false   // Pas de console logs
});
```

**Service Worker:**
Modifier `CACHE_VERSION` à chaque déploiement:

```javascript
const CACHE_VERSION = 'tinybms-v1.2.0';
```

---

## 🤝 Contribution

### Guidelines

1. **Fork** le repository
2. **Créer** feature branch (`git checkout -b feature/ma-feature`)
3. **Commit** changements (`git commit -m 'Add feature'`)
4. **Push** branch (`git push origin feature/ma-feature`)
5. **Créer** Pull Request

### Code Style

- **ESLint:** Suivre `.eslintrc.json`
- **Prettier:** Auto-format avec `.prettierrc`
- **JSDoc:** Documenter fonctions publiques

### Tests

- Ajouter tests pour nouvelles features
- Maintenir coverage > 70%
- Tests manuels sur ESP32

---

## 📄 Licence

MIT License - Voir [LICENSE](../LICENSE) pour détails.

---

## 📞 Support

- **Issues:** [GitHub Issues](https://github.com/thieryfr/TinyBMS-GW/issues)
- **Documentation:** [Wiki](https://github.com/thieryfr/TinyBMS-GW/wiki)
- **Email:** support@tinybms.com

---

## 🙏 Remerciements

- [Tabler](https://tabler.io/) - CSS Framework
- [Tabler Icons](https://tabler-icons.io/) - Icon set
- [ECharts](https://echarts.apache.org/) - Charts library
- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 framework

---

**Version:** 3.0.0
**Dernière mise à jour:** 2025-01-09
**Auteur:** TinyBMS Team
