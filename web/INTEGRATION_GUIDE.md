# Guide d'Intégration - TinyBMS Interface Web

Guide complet pour intégrer les modules UX/Performance dans vos pages.

---

## 📋 Table des Matières

1. [Installation](#installation)
2. [Initialisation Rapide](#initialisation-rapide)
3. [Modules Disponibles](#modules-disponibles)
4. [Exemples d'Intégration](#exemples-dintégration)
5. [Bonnes Pratiques](#bonnes-pratiques)
6. [Dépannage](#dépannage)

---

## 🚀 Installation

### Structure des Fichiers

```
web/
├── service-worker.js
└── src/
    └── js/
        └── utils/
            ├── notifications.js    # Système toast
            ├── loading.js          # Spinners & skeletons
            ├── theme.js            # Dark mode
            ├── i18n.js             # Internationalisation
            ├── offline.js          # Service Worker
            ├── lazy.js             # Lazy loading
            └── logger.js           # Logging structuré
```

### Import des Modules

```html
<!-- Dans votre HTML -->
<script type="module">
  import { initializeTheme } from '/src/js/utils/theme.js';
  import { initializeI18n } from '/src/js/utils/i18n.js';
  import { initializeOfflineMode } from '/src/js/utils/offline.js';
  // ... autres imports
</script>
```

---

## ⚡ Initialisation Rapide

### app.js - Point d'Entrée Principal

Créez un fichier `app.js` comme point d'entrée:

```javascript
/**
 * app.js - Application initialization
 */

import { initializeTheme } from './utils/theme.js';
import { initializeI18n } from './utils/i18n.js';
import { initializeOfflineMode, activateServiceWorkerUpdate } from './utils/offline.js';
import { notifyInfo, notifySuccess } from './utils/notifications.js';
import { configure as configureLogger, info as logInfo } from './utils/logger.js';

// Configure logger first
configureLogger({
  level: 'DEBUG', // 'DEBUG' en dev, 'INFO' en prod
  enableStorage: true,
  maxStoredLogs: 500
});

document.addEventListener('DOMContentLoaded', async () => {
  logInfo('Application starting...');

  // 1. Initialize theme
  initializeTheme({
    defaultTheme: 'auto',
    respectSystem: true,
    createToggle: true,
    toggleOptions: {
      targetSelector: '.navbar-nav',
      position: 'append'
    }
  });
  logInfo('Theme initialized');

  // 2. Initialize i18n
  initializeI18n({
    defaultLanguage: 'fr',
    respectBrowser: true,
    createSelector: true,
    selectorOptions: {
      targetSelector: '.navbar-nav',
      position: 'append',
      showFlag: true
    }
  });
  logInfo('i18n initialized');

  // 3. Initialize offline mode
  await initializeOfflineMode({
    serviceWorkerPath: '/service-worker.js',
    autoUpdate: false,
    showIndicator: true,
    onUpdate: (newWorker) => {
      notifyInfo('Mise à jour disponible', {
        duration: 0,
        actions: [
          {
            label: 'Mettre à jour',
            variant: 'primary',
            onClick: () => {
              activateServiceWorkerUpdate();
            }
          }
        ]
      });
    },
    onOnline: () => {
      notifySuccess('Connexion rétablie');
    }
  });
  logInfo('Offline mode initialized');

  // 4. Initialize page-specific features
  initializePageFeatures();

  logInfo('Application ready');
});

function initializePageFeatures() {
  // Load page-specific modules here
  const currentPage = document.body.dataset.page;

  switch (currentPage) {
    case 'dashboard':
      import('./pages/dashboard.js').then(m => m.init());
      break;
    case 'config':
      import('./pages/config.js').then(m => m.init());
      break;
    // ... autres pages
  }
}
```

### index.html - Template de Base

```html
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title data-i18n="common.app_title">TinyBMS Gateway</title>

  <!-- Tabler CSS -->
  <link rel="stylesheet" href="/src/css/tabler.min.css">
  <link rel="stylesheet" href="/src/css/tabler-icons.min.css">
</head>
<body data-page="dashboard">
  <!-- Navbar -->
  <nav class="navbar navbar-expand-lg">
    <div class="container-fluid">
      <span class="navbar-brand">TinyBMS</span>

      <!-- Navigation items -->
      <ul class="navbar-nav ms-auto">
        <!-- Theme toggle et language selector seront ajoutés ici automatiquement -->
      </ul>
    </div>
  </nav>

  <!-- Main content -->
  <main class="container-fluid mt-4">
    <div id="main-content">
      <!-- Votre contenu ici -->
    </div>
  </main>

  <!-- Scripts -->
  <script src="/src/js/tabler.min.js"></script>
  <script type="module" src="/src/js/app.js"></script>
</body>
</html>
```

---

## 📦 Modules Disponibles

### 1. Notifications

**Fichier:** `src/js/utils/notifications.js`

```javascript
import {
  showNotification,
  notifySuccess,
  notifyError,
  notifyWarning,
  notifyInfo,
  closeNotification,
  configureNotifications
} from './utils/notifications.js';

// Configuration (optionnel)
configureNotifications({
  maxVisible: 3,
  defaultDuration: 5000,
  position: 'top-right',
  animation: 'slide'
});

// Utilisation simple
notifySuccess('Configuration enregistrée');
notifyError('Connexion échouée');
notifyWarning('Batterie faible: 10%');
notifyInfo('Nouvelle version disponible');

// Notification avancée avec actions
const notifId = showNotification({
  type: 'warning',
  title: 'Confirmer la suppression',
  message: 'Cette action est irréversible',
  duration: 0, // Persistent
  closable: true,
  actions: [
    {
      label: 'Supprimer',
      variant: 'danger',
      onClick: () => deleteItem(),
      closeOnClick: true
    },
    {
      label: 'Annuler',
      variant: 'secondary'
    }
  ]
});

// Fermer manuellement
closeNotification(notifId);
```

---

### 2. Loading States

**Fichier:** `src/js/utils/loading.js`

```javascript
import {
  showSpinner,
  hideSpinner,
  setButtonLoading,
  showSkeleton,
  hideSkeleton,
  withLoading
} from './utils/loading.js';

// Spinner simple
const loadingId = showSpinner('#content');
await fetchData();
hideSpinner(loadingId);

// Spinner avec options
showSpinner('#dashboard', {
  size: 'lg',
  variant: 'primary',
  message: 'Chargement des métriques...',
  overlay: true
});

// Skeleton screen
const skelId = showSkeleton('#list-container', {
  type: 'list',
  items: 5
});
const data = await fetchList();
hideSkeleton(skelId);
renderList(data);

// Button loading
const saveBtn = document.getElementById('save-btn');
setButtonLoading(saveBtn, true);
try {
  await saveConfig();
  notifySuccess('Configuration enregistrée');
} finally {
  setButtonLoading(saveBtn, false);
}

// Wrapper automatique
const loadDashboard = withLoading(
  async () => {
    const response = await fetch('/api/dashboard');
    return response.json();
  },
  '#dashboard-container',
  { indicatorType: 'skeleton', type: 'card' }
);

const data = await loadDashboard();
```

---

### 3. Theme (Dark Mode)

**Fichier:** `src/js/utils/theme.js`

```javascript
import {
  initializeTheme,
  setTheme,
  getTheme,
  toggleThemeSimple,
  onThemeChange
} from './utils/theme.js';

// Initialisation (à faire au démarrage)
initializeTheme({
  defaultTheme: 'auto',
  respectSystem: true,
  createToggle: true,
  toggleOptions: {
    targetSelector: '.navbar-nav',
    position: 'append',
    showLabel: false
  }
});

// Changer thème manuellement
setTheme('dark');  // Force dark
setTheme('light'); // Force light
setTheme('auto');  // Suit système

// Toggle
toggleThemeSimple(); // light ↔ dark

// Écouter changements
const cleanup = onThemeChange((theme, preference) => {
  console.log(`Theme: ${theme}, Preference: ${preference}`);

  // Recharger charts si besoin
  if (theme === 'dark') {
    reloadChartsWithDarkTheme();
  }
});

// Cleanup (si nécessaire)
cleanup();
```

---

### 4. Internationalisation (i18n)

**Fichier:** `src/js/utils/i18n.js`

```javascript
import {
  initializeI18n,
  t,
  setLanguage,
  getLanguage,
  loadTranslations,
  onLanguageChange
} from './utils/i18n.js';

// Initialisation avec traductions custom
initializeI18n({
  defaultLanguage: 'fr',
  respectBrowser: true,
  translations: {
    fr: {
      dashboard: {
        title: 'Tableau de bord',
        voltage: 'Tension (V)',
        current: 'Courant (A)',
        temperature: 'Température (°C)',
        battery_status: 'État batterie: {{soc}}%'
      }
    },
    en: {
      dashboard: {
        title: 'Dashboard',
        voltage: 'Voltage (V)',
        current: 'Current (A)',
        temperature: 'Temperature (°C)',
        battery_status: 'Battery status: {{soc}}%'
      }
    }
  },
  createSelector: true
});

// Utiliser traductions en JavaScript
document.getElementById('title').textContent = t('dashboard.title');
document.getElementById('status').textContent = t('dashboard.battery_status', { soc: 75 });

// Changer langue
setLanguage('en');

// Écouter changements
onLanguageChange((lang) => {
  console.log(`Language: ${lang}`);
  reloadDynamicContent();
});
```

**Utilisation HTML:**

```html
<!-- Auto-traduction texte -->
<h1 data-i18n="dashboard.title">Dashboard</h1>

<!-- Auto-traduction placeholder -->
<input type="text" data-i18n="common.search" placeholder="Search">

<!-- Avec paramètres -->
<span data-i18n="alerts.count" data-i18n-params='{"count": 3}'>3 alerts</span>
```

---

### 5. Offline Mode

**Fichier:** `src/js/utils/offline.js`

```javascript
import {
  initializeOfflineMode,
  checkIsOnline,
  onStatusChange,
  activateServiceWorkerUpdate,
  clearAllCaches,
  getServiceWorkerVersion
} from './utils/offline.js';

// Initialisation (une seule fois au démarrage)
await initializeOfflineMode({
  serviceWorkerPath: '/service-worker.js',
  autoUpdate: false,
  showIndicator: true,
  onUpdate: (newWorker) => {
    notifyInfo('Mise à jour disponible', {
      duration: 0,
      actions: [{
        label: 'Actualiser',
        onClick: () => activateServiceWorkerUpdate()
      }]
    });
  },
  onOffline: () => {
    notifyWarning('Mode hors ligne activé');
  },
  onOnline: () => {
    notifySuccess('Connexion rétablie');
  }
});

// Vérifier statut
const online = checkIsOnline();
if (!online) {
  showOfflineWarning();
}

// Écouter changements
onStatusChange((isOnline) => {
  updateNetworkIndicator(isOnline);
});

// Actions manuelles
const version = await getServiceWorkerVersion();
console.log(`SW Version: ${version}`);

await clearAllCaches(); // Clear all caches
```

---

### 6. Lazy Loading

**Fichier:** `src/js/utils/lazy.js`

```javascript
import {
  lazyLoadModule,
  lazyLoadModules,
  lazyLoadOnVisible,
  lazyLoadCSS,
  preloadModule,
  createLazyComponent
} from './utils/lazy.js';

// Charger module lourd seulement si visible
lazyLoadOnVisible('#chart-container', async () => {
  const loadingId = showSpinner('#chart-container');

  try {
    // Charger ECharts
    const echarts = await lazyLoadModule('/src/js/lib/echarts.min.js');

    hideSpinner(loadingId);

    // Initialiser chart
    const chart = echarts.init(document.getElementById('chart-container'));
    chart.setOption(chartOptions);

    notifySuccess('Graphiques chargés');
  } catch (error) {
    hideSpinner(loadingId);
    notifyError(`Erreur: ${error.message}`);
  }
}, {
  rootMargin: '100px', // Trigger 100px avant visible
  threshold: 0.01,
  once: true
});

// Charger plusieurs modules en parallèle
const [echarts, moment] = await lazyLoadModules([
  '/src/js/lib/echarts.min.js',
  '/src/js/lib/moment.min.js'
]);

// Lazy load CSS
if (darkMode) {
  await lazyLoadCSS('/src/css/dark-charts.css');
}

// Précharger module pour plus tard
preloadModule('/src/js/advanced-features.js');

// Component wrapper réutilisable
const LazyChart = createLazyComponent(
  '/src/components/battery-chart.js',
  '<div class="skeleton skeleton-card"></div>'
);

await LazyChart('#chart-1');
```

---

### 7. Logger

**Fichier:** `src/js/utils/logger.js`

```javascript
import {
  debug,
  info,
  warn,
  error,
  configure,
  createScope,
  getHistory,
  downloadLogs
} from './utils/logger.js';

// Configuration
configure({
  level: 'DEBUG', // DEBUG, INFO, WARN, ERROR, NONE
  enableStorage: true,
  maxStoredLogs: 500,
  timestampFormat: 'iso',
  groupSimilarLogs: true
});

// Utilisation simple
debug('WebSocket message received', { message: data });
info('Configuration loaded', { config });
warn('API response slow', { duration: 3000 });
error('Failed to save', new Error('Network timeout'));

// Logger scopé (préfixe automatique)
const wsLogger = createScope('WebSocket');
wsLogger.info('Connected'); // → [INFO] [WebSocket] Connected
wsLogger.error('Connection lost', error);

// Récupérer historique
const errors = getHistory({ level: 'ERROR' });
const recent = getHistory({ limit: 10 });

// Exporter logs
downloadLogs('json'); // Télécharge fichier JSON
downloadLogs('csv');  // Télécharge fichier CSV
```

---

## 💡 Exemples d'Intégration

### Exemple 1: Page Dashboard avec Lazy Loading

```javascript
/**
 * dashboard.js
 */

import { lazyLoadOnVisible } from './utils/lazy.js';
import { showSpinner, hideSpinner } from './utils/loading.js';
import { notifySuccess, notifyError } from './utils/notifications.js';
import { info, error as logError, createScope } from './utils/logger.js';

const logger = createScope('Dashboard');

export async function init() {
  logger.info('Initializing dashboard');

  // Lazy load charts quand visible
  lazyLoadOnVisible('#battery-charts', loadCharts);
  lazyLoadOnVisible('#history-charts', loadHistoryCharts);

  // Charger données initiales
  await loadDashboardData();

  logger.info('Dashboard initialized');
}

async function loadCharts() {
  const loadingId = showSpinner('#battery-charts', {
    message: 'Chargement des graphiques...',
    overlay: true
  });

  try {
    logger.info('Loading ECharts library');

    const echarts = await import('/src/js/lib/echarts.min.js');

    hideSpinner(loadingId);

    // Initialiser charts
    const voltageChart = echarts.default.init(
      document.getElementById('voltage-chart')
    );
    const currentChart = echarts.default.init(
      document.getElementById('current-chart')
    );

    voltageChart.setOption(getVoltageChartOptions());
    currentChart.setOption(getCurrentChartOptions());

    logger.info('Charts initialized');
    notifySuccess('Graphiques chargés');
  } catch (err) {
    hideSpinner(loadingId);
    logError('Failed to load charts', err);
    notifyError('Erreur chargement graphiques');
  }
}

async function loadDashboardData() {
  try {
    const response = await fetch('/api/dashboard');
    const data = await response.json();

    updateDashboardUI(data);

    logger.info('Dashboard data loaded');
  } catch (err) {
    logError('Failed to load dashboard data', err);
    notifyError('Erreur chargement données');
  }
}
```

---

### Exemple 2: Formulaire Config avec Validation

```javascript
/**
 * config.js
 */

import { setButtonLoading } from './utils/loading.js';
import { notifySuccess, notifyError } from './utils/notifications.js';
import { t } from './utils/i18n.js';
import { info, error as logError } from './utils/logger.js';

export function init() {
  const form = document.getElementById('config-form');
  form.addEventListener('submit', handleSubmit);

  loadConfig();
}

async function loadConfig() {
  try {
    const response = await fetch('/api/config');
    const config = await response.json();

    // Remplir formulaire
    document.getElementById('mqtt_broker').value = config.mqtt_broker;
    document.getElementById('mqtt_port').value = config.mqtt_port;
    // ... autres champs

    info('Configuration loaded');
  } catch (error) {
    logError('Failed to load config', error);
    notifyError(t('config.load_error'));
  }
}

async function handleSubmit(event) {
  event.preventDefault();

  const submitBtn = event.target.querySelector('button[type="submit"]');
  setButtonLoading(submitBtn, true);

  try {
    const formData = new FormData(event.target);
    const config = Object.fromEntries(formData);

    const response = await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    });

    if (!response.ok) throw new Error('Save failed');

    info('Configuration saved', { config });
    notifySuccess(t('config.save_success'));

  } catch (error) {
    logError('Failed to save config', error);
    notifyError(t('config.save_error'));
  } finally {
    setButtonLoading(submitBtn, false);
  }
}
```

---

### Exemple 3: WebSocket avec Reconnexion

```javascript
/**
 * websocket-manager.js
 */

import { notifyWarning } from './utils/notifications.js';
import { checkIsOnline } from './utils/offline.js';
import { createScope } from './utils/logger.js';

const logger = createScope('WebSocket');

class WebSocketManager {
  constructor(url, onMessage) {
    this.url = url;
    this.onMessage = onMessage;
    this.ws = null;
    this.reconnectTimeout = null;
    this.shouldReconnect = true;
  }

  connect() {
    // Vérifier si online
    if (!checkIsOnline()) {
      logger.warn('Cannot connect: offline');
      return;
    }

    // Fermer connexion existante
    if (this.ws) {
      this.ws.close();
    }

    logger.info('Connecting to WebSocket', { url: this.url });

    this.ws = new WebSocket(this.url);

    this.ws.onopen = () => {
      logger.info('WebSocket connected');
      this.clearReconnectTimeout();
    };

    this.ws.onmessage = (event) => {
      logger.debug('Message received', { data: event.data });

      try {
        const data = JSON.parse(event.data);
        this.onMessage(data);
      } catch (error) {
        logger.error('Failed to parse message', error);
      }
    };

    this.ws.onerror = (error) => {
      logger.error('WebSocket error', error);
    };

    this.ws.onclose = () => {
      logger.info('WebSocket closed');
      this.ws = null;

      if (this.shouldReconnect) {
        this.scheduleReconnect();
      }
    };
  }

  scheduleReconnect() {
    if (this.reconnectTimeout) return;

    logger.info('Reconnecting in 5 seconds...');
    notifyWarning('Connexion perdue, reconnexion...');

    this.reconnectTimeout = setTimeout(() => {
      this.connect();
    }, 5000);
  }

  clearReconnectTimeout() {
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
  }

  disconnect() {
    this.shouldReconnect = false;
    this.clearReconnectTimeout();

    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }

    logger.info('Disconnected');
  }

  send(data) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      logger.warn('Cannot send: not connected');
      return false;
    }

    try {
      this.ws.send(JSON.stringify(data));
      logger.debug('Message sent', { data });
      return true;
    } catch (error) {
      logger.error('Failed to send message', error);
      return false;
    }
  }
}

// Usage
const wsManager = new WebSocketManager(
  'ws://192.168.1.100/ws/telemetry',
  (data) => {
    // Handle incoming data
    updateDashboard(data);
  }
);

wsManager.connect();
```

---

## ✅ Bonnes Pratiques

### 1. Initialisation

- Initialiser logger **en premier** (pour tracer tous les autres modules)
- Initialiser theme, i18n, offline mode **au démarrage** (DOMContentLoaded)
- Lazy loader les modules lourds **seulement si nécessaires**

### 2. Performance

```javascript
// ✅ BON: Lazy load
lazyLoadOnVisible('#charts', () => import('./heavy-charts.js'));

// ❌ MAUVAIS: Charger tout au démarrage
import './heavy-charts.js'; // Toujours chargé même si pas utilisé
```

### 3. Logging

```javascript
// ✅ BON: Logger scopé
const logger = createScope('ModuleName');
logger.info('Module initialized');

// ❌ MAUVAIS: console.log direct
console.log('Module initialized'); // Pas structuré, pas filtrable
```

### 4. Gestion Erreurs

```javascript
// ✅ BON: Try/catch avec log et notification
try {
  await riskyOperation();
  notifySuccess('Opération réussie');
} catch (error) {
  logError('Operation failed', error);
  notifyError('Erreur lors de l\'opération');
}

// ❌ MAUVAIS: Pas de gestion d'erreur
await riskyOperation(); // Crash silencieux si erreur
```

### 5. Cleanup

```javascript
// ✅ BON: Cleanup des listeners
const cleanup = onThemeChange((theme) => {
  // ...
});

window.addEventListener('beforeunload', () => {
  cleanup();
  wsManager.disconnect();
});

// ❌ MAUVAIS: Pas de cleanup (memory leak)
onThemeChange((theme) => {
  // ... listener jamais supprimé
});
```

---

## 🔧 Dépannage

### Problème: Notifications ne s'affichent pas

**Solution:**
```javascript
// Vérifier que le module est importé
import { notifySuccess } from './utils/notifications.js';

// Vérifier que la fonction est appelée
notifySuccess('Test'); // Devrait afficher

// Vérifier la console pour erreurs
```

### Problème: Theme ne change pas

**Solution:**
```javascript
// Vérifier initialisation
initializeTheme({ defaultTheme: 'auto' });

// Vérifier attribut HTML
console.log(document.documentElement.getAttribute('data-theme'));

// Vérifier CSS chargé
// Doit contenir [data-theme="dark"] { ... }
```

### Problème: Service Worker ne s'active pas

**Solution:**
```javascript
// Vérifier support
if ('serviceWorker' in navigator) {
  console.log('Service Worker supported');
} else {
  console.warn('Service Worker NOT supported');
}

// Vérifier HTTPS (requis sauf localhost)
if (location.protocol === 'https:' || location.hostname === 'localhost') {
  console.log('HTTPS OK');
}

// Vérifier path du SW
// Doit être à la racine: /service-worker.js
```

### Problème: Lazy loading échoue

**Solution:**
```javascript
// Vérifier path module
try {
  const module = await lazyLoadModule('/src/js/lib/echarts.min.js');
  console.log('Module loaded:', module);
} catch (error) {
  console.error('Load failed:', error);
  // Vérifier que le fichier existe et path est correct
}

// Vérifier type="module" dans <script>
// <script type="module" src="app.js"></script>
```

### Problème: Traductions ne s'appliquent pas

**Solution:**
```javascript
// Vérifier que i18n est initialisé
console.log(getLanguage()); // Doit afficher 'fr' ou 'en'

// Vérifier attribut data-i18n
// <span data-i18n="common.save">Save</span>

// Forcer mise à jour manuelle
import { updatePage } from './utils/i18n.js';
// Note: updatePage est interne, normalement auto

// Vérifier que clé existe
console.log(t('common.save')); // "Enregistrer" ou clé si manquante
```

---

## 📚 Ressources

- [Documentation Notifications](/src/js/utils/notifications.js)
- [Documentation Loading](/src/js/utils/loading.js)
- [Documentation Theme](/src/js/utils/theme.js)
- [Documentation i18n](/src/js/utils/i18n.js)
- [Documentation Offline](/src/js/utils/offline.js)
- [Documentation Lazy](/src/js/utils/lazy.js)
- [Documentation Logger](/src/js/utils/logger.js)

---

## 🤝 Support

Pour toute question ou problème:

1. Vérifier cette documentation
2. Consulter les exemples dans `/examples`
3. Vérifier logs avec `logger.downloadLogs('json')`
4. Créer une issue GitHub

---

**Dernière mise à jour:** 2025-01-09
